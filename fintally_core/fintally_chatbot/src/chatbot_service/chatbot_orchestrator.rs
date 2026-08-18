use std::sync::Arc;
use std::time::Duration;
use sqlx::PgPool;
use serde::{Deserialize, Serialize};
use futures_util::Stream;
use futures_util::StreamExt;
use async_stream::try_stream;

use crate::core::utils::errors::AppError;
use crate::chatbot_service::rag_service::RagService;
use crate::chatbot_service::vision_service::{VisionChatbotService, VisionAttachment};
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

/// Flexible container for conversational response content (Handles both raw String and Array of Strings)
#[derive(Debug, Deserialize, Serialize, Clone)]
#[serde(untagged)]
pub enum ConversationalContent {
    Text(String),
    List(Vec<String>),
    Structured { content: Vec<String> },
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct NativeLlmResponse {
    pub thought: Option<String>,
    pub tool_call: Option<ToolCallPayload>,
    pub conversational_response: Option<ConversationalContent>,
}

pub struct ChatbotOrchestrator {
    pool: PgPool,
    rag_service: Arc<RagService>,
    vision_service: Arc<VisionChatbotService>,
    model_engine: Arc<NativeLlamaEngine>,
}

impl ChatbotOrchestrator {
    pub fn new(
        pool: PgPool,
        rag_service: Arc<RagService>,
        vision_service: Arc<VisionChatbotService>,
        model_engine: Arc<NativeLlamaEngine>,
    ) -> Self {
        Self {
            pool,
            rag_service,
            vision_service,
            model_engine,
        }
    }

    pub fn chat_stream(
        self: Arc<Self>,
        user_id: i64,
        user_message: String,
        chat_history: Vec<ChatMessage>,
        attachments: Vec<VisionAttachment>,
    ) -> impl Stream<Item = Result<String, AppError>> {
        let s = try_stream! {
            // Guardrail: Max 3 file limit check
            if attachments.len() > 3 {
                yield "You can only attach up to 3 documents/images at a time. Please select fewer files and try again.".to_string();
                return;
            }

            // Check if we actually have attachments to prevent empty pipeline executions
            let has_attachments = !attachments.is_empty();

            // ── Phase 1: Context Gathering ──
            let context_timeout = Duration::from_millis(4000);
            let vision_timeout = Duration::from_millis(30000); // 30s timeout limit for Vision OCR

            let user_ctx_fut = get_user_context(&self.pool, user_id);
            let rag_ctx_fut = self.rag_service.search_knowledge(&user_message, 2);

            // Short-circuit vision processing if no files are attached
            let vision_ctx_fut = async {
                if !has_attachments {
                    Ok(String::new())
                } else {
                    // CHANGED: Switched from "format" to "plain" to improve chart/diagram reading
                    self.vision_service.process_attachments(attachments, "plain".to_string()).await
                }
            };

            let (user_context_res, rag_context_res, vision_context_res) = tokio::join!(
                tokio::time::timeout(context_timeout, user_ctx_fut),
                tokio::time::timeout(context_timeout, rag_ctx_fut),
                tokio::time::timeout(vision_timeout, vision_ctx_fut)
            );

            let user_context_raw = user_context_res.unwrap_or_default();
            let rag_context = rag_context_res.unwrap_or_default();

            // ── Vision Service Logging & Fallback Handling ──
            let vision_context = match vision_context_res {
                Ok(Ok(ctx)) => {
                    if ctx.trim().is_empty() {
                        // CHANGED: Inject an explicit LLM hint when OCR fails to prevent hallucination
                        if has_attachments {
                            eprintln!("[ORCHESTRATOR WARN] OCR processed attachments successfully, but produced no text.");
                            "[SYSTEM UNABLE TO READ IMAGE TEXT - ASK USER FOR CLEARER IMAGE OR MANUAL DETAILS]".to_string()
                        } else {
                            String::new()
                        }
                    } else {
                        println!("[ORCHESTRATOR SUCCESS] Vision extracted {} characters from attachments.", ctx.len());
                        println!("[DEBUG OCR TEXT] \n{}", ctx);
                        ctx
                    }
                }
                Ok(Err(err)) => {
                    eprintln!("[ORCHESTRATOR ERROR] Vision service processing failed: {:?}", err);
                    if has_attachments {
                        "[SYSTEM ERROR PROCESSING IMAGE - ASK USER TO RETRY OR TYPE DETAILS MANUALLY]".to_string()
                    } else {
                        String::new()
                    }
                }
                Err(_) => {
                    if has_attachments {
                        eprintln!("[ORCHESTRATOR WARN] Vision service timed out after 30 seconds.");
                        "[SYSTEM VISION TIMEOUT - ASK USER FOR SMALLER FILE OR MANUAL DETAILS]".to_string()
                    } else {
                        String::new()
                    }
                }
            };

            let user_context = format_context_for_prompt(&user_context_raw);

            // ── Phase 2: System Prompt Engineering ──
            let full_system_prompt = self.build_system_prompt(&user_context, &rag_context, &vision_context);
            let raw_prompt_str = self.build_chat_template(&user_message, &chat_history, &full_system_prompt);
            let prompt = Prompt::build(&raw_prompt_str, None)?;

            // ── Phase 3: Inference Pass ──
            let mut response_buffer = String::new();
            let stream_result = self.model_engine.stream_generate(&prompt, 1024).await;

            match stream_result {
                Ok(mut cancelable_stream) => {
                    while let Some(token_res) = cancelable_stream.stream.next().await {
                        match token_res {
                            Ok(token) => response_buffer.push_str(&token),
                            Err(e) => {
                                eprintln!("[ORCHESTRATOR ERROR] Token error: {:?}", e);
                                yield "I ran into a connection glitch while processing your files. Could you try asking once more?".to_string();
                                return;
                            }
                        }
                    }
                }
                Err(err) => {
                    eprintln!("[ORCHESTRATOR ERROR] Engine Prefill Failed: {:?}", err);
                    yield "I'm having trouble processing that prompt. Please give me a moment and try again.".to_string();
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
                            yield format!("[TOOL_CALL:{}]", tool_call.tool);
                            yield format!("[TOOL_RESULT:{}]", serde_json::to_string(&tool_result).unwrap_or_default());

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

                                    let text_out = self.extract_conversational_text(&explain_buffer);
                                    yield text_out;
                                }
                                Err(e) => {
                                    eprintln!("[ORCHESTRATOR ERROR] Explanation generation failed: {:?}", e);
                                    yield "I've calculated your financial details above, but ran into an error formatting the response text.".to_string();
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
                                "{}{}<|im_start|>user\nThe previous tool call failed validation: {}. Please provide the required sub-fields and re-issue the tool call in valid JSON format.<|im_end|>\n<|im_start|>assistant\n",
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
                                    continue;
                                }
                            }

                            yield "I couldn't complete that calculation. Mind double-checking the figures so we can try again?".to_string();
                            break;
                        }
                    }
                } else {
                    let text_out = self.extract_conversational_text(&current_response_buffer);
                    yield text_out;
                    break;
                }
            }
        };

        s
    }

    fn build_system_prompt(&self, user_context: &str, rag_context: &str, vision_context: &str) -> String {
        let mut system = String::from(r#"You are FinTally, a warm, intelligent, and highly supportive AI personal finance assistant for Indian users.

TONE & PERSONALITY GUIDELINES:
1. HUMAN-LIKE & EMPATHETIC: Speak naturally, like a knowledgeable financial advisor or friend. Avoid sounding robotic, dry, or overly formal.
2. CONTEXTUALLY AWARE: Frame amounts in Indian Rupees (₹) using standard Indian numbering terms (e.g., Lakhs, Crores) when discussing figures.
3. HELPFUL & DIRECT: Answer general financial questions, book recommendations, concept explanations, or general advice DIRECTLY without asking for numerical parameters.

OUTPUT FORMAT INSTRUCTIONS:
You MUST ALWAYS output strict JSON adhering to one of the two structures below:

Structure A - When calling a calculation tool:
{
  "thought": "Brief explanation of why this tool is selected",
  "tool_call": {
    "tool": "calculate_emi",
    "args": {
      "principal": 500000,
      "annual_rate": 8.5,
      "tenure_months": 36
    }
  },
  "conversational_response": null
}

Structure B - When answering directly without a tool:
{
  "thought": "Brief reasoning",
  "tool_call": null,
  "conversational_response": "Your full plain string response here."
}

CRITICAL SYSTEM RULES:
1. MANDATORY TOOL EXECUTION: Whenever the user asks to calculate EMI, budget, emergency fund, or tax, you MUST populate `tool_call` with the tool name and arguments. Do NOT calculate manually in `thought`.
2. NEVER LEAVE BOTH NULL: You must either provide a valid `tool_call` OR a non-null `conversational_response`.
3. CONTEXT SYNTHESIS: Read the ENTIRE chat history and attached OCR documents to collect missing parameters gracefully.
4. ANNUAL TO MONTHLY CONVERSION: Divide annual amounts by 12 automatically where appropriate.
5. STRICT ENUM VALUES: You MUST use exact string enum values specified below for tool calls.
6. MATHEMATICAL FORMULA FORMATTING: Wrap inline variables with $P$,$r$,$n$and standalone equations with$$. Never use raw LaTeX bracket delimiters.
7. CREATOR KNOWLEDGE: Speak warmly about Anshu Gondi if asked about creator/developer identity.

AVAILABLE TOOLS & REQUIRED ARGUMENTS:
- `generate_budget`: `monthly_income` (number, required), `profile` ("single_parent", "retiree_income_focused", "young_professional", "family_with_dependents")
- `calculate_emi`: `principal` (number), `annual_rate` (number), `tenure_months` (integer)
- `assess_loan`: `request` (object), `policy` ("salaried" or "self_employed")
- `emergency_fund`: `monthly_expense` (number)
- `calculate_tax`: `amount` (number), `profile` ("salaried" or "self_employed")
"#);

        system.push_str("\n\n");
        system.push_str(CREATOR_BIO);
        system.push_str("\n\n");

        // --- Prioritize Document Context First (To prevent LLM truncation) ---
        if !vision_context.trim().is_empty() {
            let truncated_vision: String = vision_context.chars().take(3000).collect();
            system.push_str(&format!(
                "\n=== ATTACHED DOCUMENT CONTEXT (OCR) ===\n{}\n=== END ATTACHED DOCUMENT CONTEXT ===\n\
                INSTRUCTION: The user has attached a document above. Read the text inside the ATTACHED DOCUMENT CONTEXT block and use it directly to answer the user's request in the `conversational_response` JSON field. \
                CRITICAL: If the user asks about a process, diagram, or non-financial details found in the document, you MUST answer it using the document context above. Do not refuse the prompt.\n\n",
                truncated_vision
            ));
        }

        if !user_context.trim().is_empty() {
            let truncated_ctx: String = user_context.chars().take(400).collect();
            system.push_str(&format_context_for_prompt(&truncated_ctx));
        }

        if !rag_context.trim().is_empty() {
            let truncated_rag: String = rag_context.chars().take(600).collect();
            system.push_str(&format!(
                "\n=== REFERENCE CONTEXT ===\n{}\n=== END REFERENCE CONTEXT ===\n",
                truncated_rag
            ));
        }

        system
    }

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
        let formatted_history = self.format_chat_history(chat_history, 1800);

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

        let formatted_history = self.format_chat_history(chat_history, 1000);
        let compact_json = serde_json::to_string(tool_result).unwrap_or_default();

        format!(
            "<|im_start|>system\n{}<|im_end|>\n{}<|im_start|>user\n{}<|im_end|>\n<|im_start|>assistant\n[Calculation completed successfully]\n\
             <|im_start|>user\nCalculation details:\n{}\n\nProvide a friendly, conversational summary of these results with practical financial context.<|im_end|>\n<|im_start|>assistant\n",
            explain_system, formatted_history, user_message, compact_json
        )
    }

    fn extract_conversational_text(&self, text: &str) -> String {
        let cleaned = self.strip_markdown(text);

        // 1. Try Structured Native Llm Response
        if let Ok(parsed) = serde_json::from_str::<NativeLlmResponse>(cleaned) {
            if let Some(resp) = parsed.conversational_response {
                let text = match resp {
                    ConversationalContent::Text(t) => t,
                    ConversationalContent::List(list) => list.join("\n"),
                    ConversationalContent::Structured { content } => content.join("\n"),
                };

                if !text.trim().is_empty() {
                    return text;
                }
            }
            if let Some(thought) = parsed.thought {
                if !thought.trim().is_empty() {
                    return thought;
                }
            }
        }

        // 2. Generic serde JSON fallback for unstructured outputs
        if let Ok(value) = serde_json::from_str::<serde_json::Value>(cleaned) {
            if let Some(conv) = value.get("conversational_response") {
                if let Some(s) = conv.as_str() {
                    if !s.trim().is_empty() {
                        return s.to_string();
                    }
                } else if let Some(arr) = conv.as_array() {
                    let joined = arr.iter().filter_map(|v| v.as_str()).collect::<Vec<_>>().join("\n");
                    if !joined.trim().is_empty() {
                        return joined;
                    }
                } else if let Some(obj) = conv.get("content").and_then(|c| c.as_array()) {
                    let joined = obj.iter().filter_map(|v| v.as_str()).collect::<Vec<_>>().join("\n");
                    if !joined.trim().is_empty() {
                        return joined;
                    }
                }
            }
            if let Some(thought) = value.get("thought").and_then(|v| v.as_str()) {
                if !thought.trim().is_empty() {
                    return thought.to_string();
                }
            }
        }

        // 3. Plain Text Fallback
        if cleaned.is_empty() {
            return "I processed your request, but could not format the output. Could you try asking your question again?".to_string();
        }

        cleaned.to_string()
    }

    fn strip_markdown<'a>(&self, text: &'a str) -> &'a str {
        let mut cleaned = text.trim();
        if cleaned.starts_with("```json") {
            cleaned = &cleaned[7..];
        } else if cleaned.starts_with("```") {
            cleaned = &cleaned[3..];
        }
        if cleaned.ends_with("```") {
            cleaned = &cleaned[..cleaned.len() - 3];
        }
        cleaned.trim()
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
