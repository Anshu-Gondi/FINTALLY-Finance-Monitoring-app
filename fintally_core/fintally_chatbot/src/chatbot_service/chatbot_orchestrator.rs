use std::sync::Arc;
use std::time::Duration;
use uuid::Uuid;
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
        user_id: Uuid,
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

            // ── Phase 3: First Inference Pass (Silently Accumulated) ──
            let mut response_buffer = String::new();
            let stream_result = self.model_engine.stream_generate(&prompt, 1024).await;

            match stream_result {
                Ok(mut cancelable_stream) => {
                    while let Some(token_res) = cancelable_stream.stream.next().await {
                        match token_res {
                            Ok(token) => {
                                // Accumulate tokens internally; NEVER yield to UI here
                                response_buffer.push_str(&token);
                            }
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
                println!("[ORCHESTRATOR] Identified tool call: {}", tool_call.tool);

                let tool_result = match Planner::execute(&tool_call.tool, tool_call.args).await {
                    Ok(res) => res,
                    Err(err) => serde_json::json!({ "error": err.to_string() }),
                };

                // ── Phase 5: Second Pass (Explanation Generation) ──
                let explain_prompt_str = self.build_explain_prompt(&user_message, &tool_call.tool, &tool_result, &chat_history, &full_system_prompt);
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
                        yield "I calculated the financial data, but encountered an error formatting the final explanation.".to_string();
                    }
                }
            } else {
                // ── No Tool Executed: Extract Conversational Text Safely ──
                let text_out = self.extract_conversational_text(&response_buffer);
                yield text_out;
            }
        };

        s
    }

    /// Safely extracts conversational output without leaking JSON syntax
    fn extract_conversational_text(&self, text: &str) -> String {
        let cleaned = self.strip_markdown(text);

        // 1. Try standard JSON parsing
        if let Ok(parsed) = serde_json::from_str::<NativeLlmResponse>(cleaned) {
            if let Some(resp) = parsed.conversational_response {
                if !resp.trim().is_empty() {
                    return resp;
                }
            }
            if let Some(thought) = parsed.thought {
                if !thought.trim().is_empty() {
                    return thought;
                }
            }
        }

        // 2. Try partial regex/substring parsing if JSON was cut off or malformed
        if let Some(pos) = cleaned.find("\"conversational_response\":") {
            let slice = &cleaned[pos + 26..];
            let trimmed = slice.trim().trim_start_matches('"');
            if let Some(end) = trimmed.find("\",") {
                return trimmed[..end].replace("\\n", "\n").replace("\\\"", "\"").to_string();
            } else if let Some(end) = trimmed.rfind('"') {
                return trimmed[..end].replace("\\n", "\n").replace("\\\"", "\"").to_string();
            }
        }

        // 3. Fallback: If it's pure raw JSON that couldn't be parsed, do not yield raw JSON
        if cleaned.starts_with('{') && cleaned.contains("\"thought\"") {
            return "I have analyzed your query and structured your financial request. Please provide any additional missing context if required.".to_string();
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

    fn build_system_prompt(&self, user_context: &str, rag_context: &str) -> String {
        let mut system = String::from(
            "You are FinTally, a highly capable personal finance assistant explicitly optimized for Indian users.\n\
             You speak plainly, use ₹ for amounts, provide practical localized advice, and communicate structurally using strict JSON formatting.\n\n\
             Your output must ALWAYS be a valid JSON object matching this exact schema:\n\
             {\n\
               \"thought\": \"Brief step-by-step internal reasoning or context validation\",\n\
               \"tool_call\": {\n\
                 \"tool\": \"TOOL_NAME\",\n\
                 \"args\": {}\n\
               },\n\
               \"conversational_response\": \"Plaintext conversational response, financial explanation, or null if executing a tool\"\n\
             }\n\n\
             RULES FOR TOOL CALLS:\n\
             1. Use EXACT tool names (e.g. `generate_investment_plan`, NOT `generate investment plan`).\n\
             2. When a tool call is needed, set \"conversational_response\" to null.\n\n\
             Available Tools:\n\
             - `calculate_emi`: Computes loan repayment. Requires: principal (number), annual_rate (number), tenure_months (integer).\n\
             - `assess_loan`: Evaluates loan eligibility matrices. Requires: request (object), policy (\"salaried\"|\"self_employed\").\n\
             - `emergency_fund`: Recommends safety net buffers. Requires: monthly_expense (number).\n\
             - `savings_projection`: Models growth over timeline. Requires: months (integer).\n\
             - `calculate_tax`: Estimates Indian income tax liability. Requires: amount (number), profile (\"salaried\"|\"self_employed\").\n\
             - `generate_investment_plan`: Deploys risk-allocated portfolios. Requires: investable_amount (number), profile (\"young_professional\"|\"family_with_dependents\"|\"retiree_income_focused\"|\"single_parent\").\n\
             - `generate_cashflow`: Extrapolates monthly inflows/outflows. Requires: monthly_income (number), profile.\n\
             - `generate_budget`: Drafts a contextual budget layout. Requires: monthly_income (number), profile.\n\
             - `stat_analysis`: Computes peer benchmark parameters. Requires: profile.\n\n\
             RULES FOR GENERAL QUESTIONS:\n\
             If no calculation tool is required, set \"tool_call\": null and write the response in \"conversational_response\".\n\n\
             EXAMPLES:\n\
             User: EMI for 5 lakh at 8.5% for 5 years?\n\
             Assistant:\n\
             {\n\
               \"thought\": \"Calculating EMI for 500,000 at 8.5% over 60 months.\",\n\
               \"tool_call\": { \"tool\": \"calculate_emi\", \"args\": { \"principal\": 500000, \"annual_rate\": 8.5, \"tenure_months\": 60 } },\n\
               \"conversational_response\": null\n\
             }\n"
        );

        if !user_context.trim().is_empty() {
            system.push_str(&format_context_for_prompt(user_context));
        }

        if !rag_context.trim().is_empty() {
            system.push_str(&format!(
                "\nIMPORTANT: You have access to real-time updated reference documents below. \
                Use these documents as your primary source of truth. Ignore any past pre-training knowledge cutoffs if the documents state newer information.\n\n\
                === RELEVANT DOCUMENT REFERENCE CONTEXT ===\n{}\n=== END DOCUMENT REFERENCE ===\n",
                rag_context
            ));
        }

        system
    }

    fn build_chat_template(&self, user_message: &str, chat_history: &[ChatMessage], system_prompt: &str) -> String {
        let mut template = format!("<|im_start|>system\n{}<|im_end|>\n", system_prompt);
        let historical_slice = if chat_history.len() > 6 { &chat_history[chat_history.len() - 6..] } else { chat_history };
        for msg in historical_slice {
            template.push_str(&format!("<|im_start|>{}\n{}<|im_end|>\n", msg.role, msg.content));
        }
        template.push_str(&format!("<|im_start|>user\n{}<|im_end|>\n<|im_start|>assistant\n", user_message));
        template
    }

    fn build_explain_prompt(
        &self,
        user_message: &str,
        tool_name: &str,
        tool_result: &serde_json::Value,
        chat_history: &[ChatMessage],
        system_prompt: &str
    ) -> String {
        let mut template = format!("<|im_start|>system\n{}<|im_end|>\n", system_prompt);

        let historical_slice = if chat_history.len() > 4 {
            &chat_history[chat_history.len() - 4..]
        } else {
            chat_history
        };

        for msg in historical_slice {
            template.push_str(&format!("<|im_start|>{}\n{}<|im_end|>\n", msg.role, msg.content));
        }

        template.push_str(&format!(
            "<|im_start|>user\n{}<|im_end|>\n<|im_start|>assistant\n[Calculation complete]<|im_end|>\n\
             <|im_start|>user\nHere is the verified financial calculation data:\n{}\n\n\
INSTRUCTIONS FOR YOUR RESPONSE:
1. Provide direct financial advice formatted nicely for the user.
2. DO NOT mention internal terms like 'tool', 'tool_call', or function name '{}'.
3. Present all amounts formatted in Rupees (₹).
4. Return your output inside the \"conversational_response\" JSON key.<|im_end|>\n<|im_start|>assistant\n",
            user_message,
            serde_json::to_string_pretty(tool_result).unwrap_or_default(),
            tool_name
        ));

        template
    }

    fn extract_structural_tool_call(&self, text: &str) -> Option<ToolCallPayload> {
        let cleaned = self.strip_markdown(text);

        // Attempt direct JSON deserialize
        if let Ok(parsed) = serde_json::from_str::<NativeLlmResponse>(cleaned) {
            if let Some(mut tool) = parsed.tool_call {
                if !tool.tool.is_empty() && tool.tool != "null" {
                    // Standardize tool name in case model substituted spaces for underscores
                    tool.tool = tool.tool.replace(' ', "_");
                    return Some(tool);
                }
            }
        }

        // Manual extraction fallback if JSON token stream was cut short
        if let Some(tool_pos) = cleaned.find("\"tool\":") {
            let slice = &cleaned[tool_pos + 7..];
            let trimmed = slice.trim().trim_start_matches('"');
            if let Some(end) = trimmed.find('"') {
                let tool_name = trimmed[..end].replace(' ', "_");
                if !tool_name.is_empty() && tool_name != "null" {
                    return Some(ToolCallPayload {
                        tool: tool_name,
                        args: serde_json::json!({}),
                    });
                }
            }
        }

        None
    }
}
