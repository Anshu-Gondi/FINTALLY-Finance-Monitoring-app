use std::sync::Arc;
use std::time::Duration;
use sqlx::PgPool;
use serde::{Deserialize, Serialize};
use futures_util::Stream;
use futures_util::StreamExt;
use async_stream::try_stream;

use crate::core::utils::errors::AppError;
use crate::chatbot_service::rag_service::RagService;
use crate::chatbot_service::user_context::{get_user_context, format_context_for_prompt};
use crate::chatbot_service::creator_bio::CREATOR_BIO;
use crate::core::llm::native_engine::NativeLlamaEngine;
use crate::core::llm::engine::LlmEngine;

use crate::core::llm::prompt::Prompt;
use crate::core::llm::planner::Planner;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ChatMessage {
    pub role: String,
    pub content: String,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct ToolCallPayload {
    pub tool: String,
    pub args: serde_json::Value,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct NativeLlmResponse {
    pub thought: Option<String>,
    pub tool_call: Option<ToolCallPayload>,
    pub conversational_response: Option<String>,
}

pub struct ChatbotOrchestrator {
    pool: PgPool,
    rag_service: Arc<RagService>,
    model_engine: Arc<NativeLlamaEngine>,
}

impl ChatbotOrchestrator {
    pub fn new(pool: PgPool, rag_service: Arc<RagService>, model_engine: Arc<NativeLlamaEngine>) -> Self {
        Self { pool, rag_service, model_engine }
    }

    pub fn chat_stream(
        self: Arc<Self>,
        user_id: i64,
        user_message: String,
        chat_history: Vec<ChatMessage>,
    ) -> impl Stream<Item = Result<String, AppError>> {
        let s = try_stream! {
            // ── Phase 1: Context Gathering ──
            let context_timeout = Duration::from_millis(4000);

            let user_ctx_fut = get_user_context(&self.pool, user_id);
            let rag_ctx_fut = self.rag_service.search_knowledge(&user_message, 2);

            let (user_context_res, rag_context_res) = tokio::join!(
                tokio::time::timeout(context_timeout, user_ctx_fut),
                tokio::time::timeout(context_timeout, rag_ctx_fut)
            );

            let user_context = user_context_res.unwrap_or_default();
            let rag_context = rag_context_res.unwrap_or_default();

            // ── Phase 2: System Prompt Engineering ──
            let full_system_prompt = self.build_system_prompt(&user_context, &rag_context);
            let raw_prompt_str = self.build_chat_template(&user_message, &chat_history, &full_system_prompt);

            let prompt = Prompt::build(&raw_prompt_str, None)?;

            // ── Phase 3: First Inference Pass (Accumulation) ──
            let mut response_buffer = String::new();
            let stream_result = self.model_engine.stream_generate(&prompt, 1024).await;

            match stream_result {
                Ok(mut cancelable_stream) => {
                    while let Some(token_res) = cancelable_stream.stream.next().await {
                        match token_res {
                            Ok(token) => response_buffer.push_str(&token),
                            Err(e) => {
                                eprintln!("[ORCHESTRATOR ERROR] Token error: {:?}", e);
                                yield "I ran into a quick connection glitch while typing. Could you try asking that once more?".to_string();
                                return;
                            }
                        }
                    }
                }
                Err(err) => {
                    eprintln!("[ORCHESTRATOR ERROR] Engine Prefill Failed: {:?}", err);
                    yield "I'm having a little trouble gathering my thoughts right now. Please give me a moment and try again.".to_string();
                    return;
                }
            }

            // ── Phase 4: Native JSON Tool Parsing, Validation & Self-Correction Loop ──
            let mut current_response_buffer = response_buffer;
            let mut max_retries = 2;
            let mut retry_history = Vec::new();

            loop {
                if let Some(tool_call) = self.extract_structural_tool_call(&current_response_buffer) {
                    println!("[ORCHESTRATOR] Identified tool call: {} with args: {:?}", tool_call.tool, tool_call.args);

                    match Planner::execute(&tool_call.tool, tool_call.args.clone()).await {
                        Ok(tool_result) => {
                            // Yield clean tool execution metadata tags for UI component handling
                            yield format!("[TOOL_CALL:{}]", tool_call.tool);
                            yield format!("[TOOL_RESULT:{}]", serde_json::to_string(&tool_result).unwrap_or_default());

                            // ── Phase 5: Second Pass (Humanized Explanation Generation) ──
                            let explain_prompt_str = self.build_explain_prompt(&user_message, &tool_call.tool, &tool_result, &chat_history);
                            let explain_prompt = Prompt::build(&explain_prompt_str, None)?;

                            match self.model_engine.stream_generate(&explain_prompt, 1024).await {
                                Ok(mut explain_stream) => {
                                    let mut explain_buffer = String::new();
                                    while let Some(token_res) = explain_stream.stream.next().await {
                                        if let Ok(token) = token_res {
                                            explain_buffer.push_str(&token);
                                        }
                                    }

                                    // Extract clean output from second-pass explanation
                                    let text_out = self.extract_conversational_text(&explain_buffer);
                                    yield text_out;
                                }
                                Err(e) => {
                                    eprintln!("[ORCHESTRATOR ERROR] Explanation generation failed: {:?}", e);
                                    yield "I've calculated your financial details above, but I ran into a small error formatting the summary text for you.".to_string();
                                }
                            }
                            break;
                        }
                        Err(diagnostic_report) => {
                            eprintln!(
                                "[ORCHESTRATOR DIAGNOSTIC] Intercepted tool error: [{:?}] {}",
                                diagnostic_report.status, diagnostic_report.error_summary
                            );

                            if max_retries == 0 {
                                yield format!(
                                    "I was trying to run the calculations for you, but I'm missing a few exact details. {}",
                                    diagnostic_report.remediation_prompt
                                );
                                break;
                            }

                            max_retries -= 1;

                            retry_history.push(format!(
                                "<|im_start|>assistant\n{}\n<|im_end|>\n<|im_start|>tool\n{}\n<|im_end|>\n",
                                current_response_buffer.trim(),
                                serde_json::to_string(&diagnostic_report.to_llm_payload()).unwrap_or_default()
                            ));

                            let retry_prompt_str = format!(
                                "{}{}<|im_start|>user\nThe previous tool call failed validation: {}. Please provide the required sub-fields or adjust the profile parameter and re-issue the tool call in valid JSON format.<|im_end|>\n<|im_start|>assistant\n",
                                raw_prompt_str,
                                retry_history.join(""),
                                diagnostic_report.error_summary
                            );

                            if let Ok(retry_prompt) = Prompt::build(&retry_prompt_str, None) {
                                if let Ok(mut retry_stream) = self.model_engine.stream_generate(&retry_prompt, 1024).await {
                                    let mut retry_buffer = String::new();
                                    while let Some(token_res) = retry_stream.stream.next().await {
                                        if let Ok(token) = token_res {
                                            retry_buffer.push_str(&token);
                                        }
                                    }
                                    current_response_buffer = retry_buffer;
                                    continue; // Re-evaluate tool call from LLM's corrected output
                                }
                            }

                            yield "I couldn't quite complete that calculation. Mind double-checking the figures you provided so we can try again?".to_string();
                            break;
                        }
                    }
                } else {
                    // Extract clean conversational output before yielding to SSE/client
                    let text_out = self.extract_conversational_text(&current_response_buffer);
                    yield text_out;
                    break;
                }
            }
        };

        s
    }

    fn build_system_prompt(&self, user_context: &str, rag_context: &str) -> String {
        let mut system = String::from(r#"You are FinTally, a warm, intelligent, and highly supportive AI personal finance assistant for Indian users.

TONE & PERSONALITY GUIDELINES:
1. HUMAN-LIKE & EMPATHETIC: Speak naturally, like a knowledgeable financial advisor or friend. Avoid sounding robotic, dry, or overly formal.
2. CONTEXTUALLY AWARE: Frame amounts in Indian Rupees (₹) using standard Indian numbering terms where appropriate (e.g., Lakhs, Crores) when discussing figures.
3. HELPFUL & DIRECT: Answer general financial questions, book recommendations, concept explanations, or general advice DIRECTLY without asking for numerical parameters.

TECHNICAL INSTRUCTION:
You communicate via strict JSON output formatting. Your response MUST strictly adhere to this JSON format:
{
  "thought": "Step-by-step reasoning summarizing conversation history and parameter extraction",
  "tool_call": null,
  "conversational_response": "Your full text response here. If no tool is needed, provide your complete answer here."
}

CRITICAL SYSTEM RULES:
1. TOOL CALLS ARE OPTIONAL: ONLY populate "tool_call" with {"tool": "NAME", "args": {...}} if the user explicitly requests a calculation, budget generation, EMI estimate, or loan assessment. For general knowledge, book recommendations, or general guidance, set "tool_call": null and put your full response in "conversational_response".
2. NO UNNECESSARY PARAMETER REQUESTS: Do NOT demand monthly income or profile parameters unless executing a calculation tool.
3. CONTEXT SYNTHESIS: Read the ENTIRE chat history to collect missing parameters gracefully when a tool IS requested.
4. ANNUAL TO MONTHLY CONVERSION: Divide annual amounts by 12 automatically.
5. STRICT ENUM VALUES: You MUST use exact string enum values specified below for tool calls.
6. TOOL EXECUTION: When executing a tool call, set "conversational_response": null.

7. MATHEMATICAL FORMULA FORMATTING:
   - Wrap inline variables with single dollar signs: $P$, $r$, $n$.
   - Wrap standalone equations with double dollar signs:
     $$\text{Profit} = \text{Revenue} - \text{Expenses}$$
   - NEVER use square brackets [ ... ] or parenthesis ( P ) for math.
   - NEVER use raw LaTeX bracket delimiters \[...\] or \(...\).
8. CREATOR & DEVELOPER KNOWLEDGE:
    When users ask who created, built, or developed FinTally, or ask about the developer/creator behind this platform, speak warmly about Anshu Gondi using the context provided below.

AVAILABLE TOOLS & REQUIRED ARGUMENTS:
- `generate_budget`:
  * `monthly_income` (number, required)
  * `profile` (string, required): Options: "single_parent", "retiree_income_focused", "young_professional", "family_with_dependents".
  * NOTE: If using "young_professional", `tax_profile` ("salaried" or "self_employed") must be provided in args.
  * NOTE: If using "family_with_dependents", `loan_policy` ("salaried" or "self_employed") must be provided in args.
  * If tax_profile/loan_policy are not specified, prefer "single_parent" or "retiree_income_focused".
- `calculate_emi`: `principal` (number), `annual_rate` (number), `tenure_months` (integer)
- `assess_loan`: `request` (object), `policy` ("salaried" or "self_employed")
- `emergency_fund`: `monthly_expense` (number)
- `calculate_tax`: `amount` (number), `profile` ("salaried" or "self_employed")
"#);

        // Append the constant creator bio
        system.push_str("\n\n");
        system.push_str(CREATOR_BIO);
        system.push_str("\n\n");

        if !user_context.trim().is_empty() {
            let truncated_ctx: String = user_context.chars().take(500).collect();
            system.push_str(&format_context_for_prompt(&truncated_ctx));
        }

        if !rag_context.trim().is_empty() {
            let truncated_rag: String = rag_context.chars().take(800).collect();
            system.push_str(&format!(
                "\n=== REFERENCE CONTEXT ===\n{}\n=== END REFERENCE CONTEXT ===\n",
                truncated_rag
            ));
        }

        system
    }

    /// Trims chat history dynamically using a character budget to prevent context overflow.
    fn format_chat_history(&self, chat_history: &[ChatMessage], max_chars: usize) -> String {
        let mut history_str = String::new();
        let mut current_len = 0;

        for msg in chat_history.iter().rev() {
            let clean_content = self.extract_conversational_text(&msg.content);
            let formatted = format!("<|im_start|>{}\n{}<|im_end|>\n", msg.role, clean_content);

            if current_len + formatted.len() > max_chars {
                break;
            }

            current_len += formatted.len();
            history_str.insert_str(0, &formatted);
        }

        history_str
    }

    fn build_chat_template(&self, user_message: &str, chat_history: &[ChatMessage], system_prompt: &str) -> String {
        let formatted_history = self.format_chat_history(chat_history, 2500);

        format!(
            "<|im_start|>system\n{}<|im_end|>\n{}<|im_start|>user\n{}<|im_end|>\n<|im_start|>assistant\n",
            system_prompt, formatted_history, user_message
        )
    }

    fn build_explain_prompt(
        &self,
        user_message: &str,
        _tool_name: &str,
        tool_result: &serde_json::Value,
        chat_history: &[ChatMessage],
    ) -> String {
        let explain_system = "You are FinTally, an encouraging and empathetic AI personal finance assistant for Indian users. \
        Explain calculation results warmly, conversationally, and clearly in Indian Rupees (₹). \
        Highlight actionable insights or practical financial advice based on the numbers calculated, and keep it easy to read.";

        let formatted_history = self.format_chat_history(chat_history, 1200);
        let compact_json = serde_json::to_string(tool_result).unwrap_or_default();

        format!(
            "<|im_start|>system\n{}<|im_end|>\n{}<|im_start|>user\n{}<|im_end|>\n<|im_start|>assistant\n[Calculation completed successfully]\n\
             <|im_start|>user\nCalculation details:\n{}\n\nProvide a friendly, conversational summary of these results with practical financial context.<|im_end|>\n<|im_start|>assistant\n",
            explain_system, formatted_history, user_message, compact_json
        )
    }

    fn extract_conversational_text(&self, text: &str) -> String {
        let cleaned = self.strip_markdown(text);

        // 1. Try parsing exact NativeLlmResponse struct
        if let Ok(parsed) = serde_json::from_str::<NativeLlmResponse>(cleaned) {
            if let Some(resp) = parsed.conversational_response {
                if !resp.trim().is_empty() {
                    return resp;
                }
            }
        }

        // 2. Fallback for nested objects inside `conversational_response` (e.g. {"summary": "..."})
        if let Ok(value) = serde_json::from_str::<serde_json::Value>(cleaned) {
            if let Some(conv) = value.get("conversational_response") {
                // If conversational_response is a string, return it
                if let Some(s) = conv.as_str() {
                    if !s.trim().is_empty() {
                        return s.to_string();
                    }
                }
                // If conversational_response is a nested object with "summary" / "text" fields
                if let Some(obj) = conv.as_object() {
                    let mut parts = Vec::new();
                    if let Some(summary) = obj.get("summary").and_then(|v| v.as_str()) {
                        parts.push(summary.to_string());
                    }
                    if let Some(details) = obj.get("details") {
                        if let Ok(pretty) = serde_json::to_string_pretty(details) {
                            parts.push(pretty);
                        }
                    }
                    if !parts.is_empty() {
                        return parts.join("\n\n");
                    }
                }
            }
        }

        // 3. Regex fallback for incomplete JSON stream or syntax errors
        let re = regex::Regex::new(r#""conversational_response"\s*:\s*"([^"]+)""#).unwrap();
        if let Some(caps) = re.captures(cleaned) {
            if let Some(matched) = caps.get(1) {
                return matched.as_str().to_string();
            }
        }

        cleaned.to_string()
    }

    fn strip_markdown<'a>(&self, text: &'a str) -> &'a str {
        let cleaned = text.trim();
        cleaned
            .strip_prefix("```json").unwrap_or(cleaned)
            .strip_prefix("```").unwrap_or(cleaned)
            .strip_suffix("```").unwrap_or(cleaned)
            .trim()
    }

    fn extract_structural_tool_call(&self, text: &str) -> Option<ToolCallPayload> {
        let cleaned = self.strip_markdown(text);

        if let Ok(parsed) = serde_json::from_str::<NativeLlmResponse>(cleaned) {
            if let Some(mut tool) = parsed.tool_call {
                if !tool.tool.is_empty() && tool.tool != "null" {
                    tool.tool = tool.tool.replace(' ', "_");
                    return Some(tool);
                }
            }
        }

        None
    }
}
