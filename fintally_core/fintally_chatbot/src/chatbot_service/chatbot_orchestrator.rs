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

            // ── Phase 3: First Inference Pass ──
            let mut response_buffer = String::new();
            let stream_result = self.model_engine.stream_generate(&prompt, 1024).await;

            match stream_result {
                Ok(mut cancelable_stream) => {
                    while let Some(token_res) = cancelable_stream.stream.next().await {
                        match token_res {
                            Ok(token) => response_buffer.push_str(&token),
                            Err(e) => {
                                eprintln!("[ORCHESTRATOR ERROR] Token error: {:?}", e);
                                yield format!("[ERROR: Token generation failed]");
                                return;
                            }
                        }
                    }
                }
                Err(err) => {
                    eprintln!("[ORCHESTRATOR ERROR] Engine Prefill Failed: {:?}", err);
                    yield format!("[ERROR: Dynamic prefill failed]");
                    return;
                }
            }

            // ── Phase 4: Native JSON Tool Parsing & Execution ──
            if let Some(tool_call) = self.extract_structural_tool_call(&response_buffer) {
                println!("[ORCHESTRATOR] Identified tool call: {} with args: {:?}", tool_call.tool, tool_call.args);

                let tool_result = match Planner::execute(&tool_call.tool, tool_call.args).await {
                    Ok(res) => res,
                    Err(err) => serde_json::json!({ "error": err.to_string() }),
                };

                let is_error = tool_result.get("error").is_some() || tool_result.get("CALC_ERR").is_some();

                yield format!("[TOOL_CALL:{}]", tool_call.tool);
                yield format!("[TOOL_RESULT:{}]", serde_json::to_string(&tool_result).unwrap_or_default());

                if is_error {
                    let err_msg = tool_result
                        .get("error")
                        .or_else(|| tool_result.get("CALC_ERR"))
                        .and_then(|v| v.as_str())
                        .unwrap_or("Invalid input values.");

                    yield format!("I couldn't process that calculation: {}. Please double check the details provided.", err_msg);
                } else {
                    // ── Phase 5: Second Pass (Streamlined Explanation Generation) ──
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
                            yield "I calculated the financial data, but encountered an error formatting the summary.".to_string();
                        }
                    }
                }
            } else {
                let text_out = self.extract_conversational_text(&response_buffer);
                yield text_out;
            }
        };

        s
    }

    fn build_system_prompt(&self, user_context: &str, rag_context: &str) -> String {
        let mut system = String::from(
            "You are FinTally, an AI personal finance assistant for Indian users.\n\
             Communicate clearly using strict JSON output formatting.\n\n\
             Your response MUST strictly adhere to this JSON format:\n\
             {\n\
               \"thought\": \"Step-by-step reasoning summarizing conversation history and parameter extraction\",\n\
               \"tool_call\": {\n\
                 \"tool\": \"EXACT_TOOL_NAME\",\n\
                 \"args\": {}\n\
               },\n\
               \"conversational_response\": \"Conversational reply or polite request for missing inputs\"\n\
             }\n\n\
             CRITICAL SYSTEM RULES:\n\
             1. CONTEXT SYNTHESIS: Read the ENTIRE chat history to collect missing parameters.\n\
             2. ANNUAL TO MONTHLY CONVERSION: Divide annual amounts by 12.\n\
             3. STRICT ENUM VALUES: You MUST use exact string enum values specified below.\n\
             4. NO HALLUCINATED LIMITATIONS: Never claim you cannot calculate large numbers.\n\
             5. TOOL EXECUTION: When executing a tool call, set \"conversational_response\": null.\n\n\
             6. MATHEMATICAL FORMULA FORMATTING:\n\
                - ALWAYS wrap inline variables with single dollar signs: `$P$`, `$r$`, `$n$`.\n\
                - ALWAYS wrap standalone equations with double dollar signs:\n\
                  $$\n\
                  \\text{Profit} = \\text{Revenue} - \\text{Expenses}\n\
                  $$\n\
                - NEVER use square brackets `[ ... ]` or parenthesis `( P )` for math.\n\
                - NEVER use raw LaTeX bracket delimiters `\\[ ... \\]` or `\\( ... \\)`.\n\
                - NEVER add standalone or trailing backslashes `\\` before or after equations.\n\n
             AVAILABLE TOOLS & REQUIRED ARGUMENTS:\n\
             - `generate_budget`: `monthly_income` (number), `profile` (\"family_with_dependents\", \"young_professional\", \"single_parent\", \"retiree_income_focused\")\n\
             - `calculate_emi`: `principal` (number), `annual_rate` (number), `tenure_months` (integer)\n\
             - `assess_loan`: `request` (object), `policy` (\"salaried\" or \"self_employed\")\n\
             - `emergency_fund`: `monthly_expense` (number)\n\
             - `calculate_tax`: `amount` (number), `profile` (\"salaried\" or \"self_employed\")\n"
        );

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

        // Iterate backwards from recent messages
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
        // Enforce a maximum of ~2500 characters for chat history (roughly 600-800 tokens)
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
        // Lightweight system prompt for Phase 5 to avoid context blowup
        let explain_system = "You are FinTally, an AI personal finance assistant for Indian users. \
        Explain calculation results clearly and warmly using Indian Rupees (₹). Keep responses concise.";

        let formatted_history = self.format_chat_history(chat_history, 1200);
        let compact_json = serde_json::to_string(tool_result).unwrap_or_default();

        format!(
            "<|im_start|>system\n{}<|im_end|>\n{}<|im_start|>user\n{}<|im_end|>\n<|im_start|>assistant\n[Calculation completed successfully]\n\
             <|im_start|>user\nCalculation details:\n{}\n\nProvide a friendly summary of this result in Indian Rupees (₹).<|im_end|>\n<|im_start|>assistant\n",
            explain_system, formatted_history, user_message, compact_json
        )
    }

    fn extract_conversational_text(&self, text: &str) -> String {
        let cleaned = self.strip_markdown(text);

        if let Ok(parsed) = serde_json::from_str::<NativeLlmResponse>(cleaned) {
            if let Some(resp) = parsed.conversational_response {
                if !resp.trim().is_empty() {
                    return resp;
                }
            }
        }

        if let Ok(value) = serde_json::from_str::<serde_json::Value>(cleaned) {
            if let Some(msg) = value.get("conversational_response").or_else(|| value.get("message")).and_then(|v| v.as_str()) {
                if !msg.trim().is_empty() {
                    return msg.to_string();
                }
            }
        }

        if cleaned.starts_with('{') && cleaned.contains("\"thought\"") {
            return "Could you please clarify your monthly income and financial goal so I can calculate this for you?".to_string();
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
