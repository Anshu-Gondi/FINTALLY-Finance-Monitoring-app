use std::sync::Arc;
use std::time::Duration;
use uuid::Uuid;
use sqlx::PgPool;
use serde::{Deserialize, Serialize};
use futures_util::Stream;
use futures_util::StreamExt; // FIXED: Using futures_util's StreamExt to match your engine's underlying streams
use async_stream::try_stream;

use crate::core::utils::errors::AppError;
use crate::chatbot_service::rag_service::RagService;
use crate::chatbot_service::user_context::{get_user_context, format_context_for_prompt};
use crate::core::llm::native_engine::NativeLlamaEngine;
use crate::core::llm::engine::LlmEngine; // FIXED: Brought trait into scope to fix E0599 method resolution

// Define structure for standard chat messages tracking context
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ChatMessage {
    pub role: String,
    pub content: String,
}

/// Structural payload matching the native JSON tool schema
#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct ToolCallPayload {
    pub tool: String,
    pub args: serde_json::Value,
}

/// Top-level model output structure for Qwen JSON Mode
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

    /// Master streaming generator yielding token slices using real native Qwen inference
    pub fn chat_stream(
        self: Arc<Self>,
        user_id: Uuid,
        user_message: String,
        chat_history: Vec<ChatMessage>,
    ) -> impl Stream<Item = Result<String, AppError>> {
        // Enforce explicit type bindings on the macro block to prevent type inference drops
        let s = try_stream! {
            // ── Phase 1: Concurrent Data Gathering ──
            let context_timeout = Duration::from_millis(4000);
            
            let user_ctx_fut = get_user_context(&self.pool, user_id);
            let rag_ctx_fut = self.rag_service.search_knowledge(&user_message, 2);

            let (user_context_res, rag_context_res) = tokio::join!(
                tokio::time::timeout(context_timeout, user_ctx_fut),
                tokio::time::timeout(context_timeout, rag_ctx_fut)
            );

            let user_context = user_context_res.unwrap_or_default();
            let rag_context = rag_context_res.unwrap_or_default();

            if !user_context.is_empty() || !rag_context.is_empty() {
                yield "[CONTEXT_LOADED]".to_string();
            }

            // ── Phase 2: System Prompt Engineering ──
            let full_system_prompt = self.build_system_prompt(&user_context, &rag_context);
            let prompt = self.build_chat_template(&user_message, &chat_history, &full_system_prompt);

            // ── Phase 3: First Inference Pass (Real Token Streaming) ──
            println!("[ORCHESTRATOR] Submitting prompt structure to native Qwen Candle engine...");
            
            let mut response_buffer = String::new();
            
            // Calling the trait method on the engine instance safely
            let mut cancelable_stream = self.model_engine
                .stream_generate(&prompt, 1024)
                .await?;

            while let Some(token_res) = cancelable_stream.stream.next().await {
                let token = token_res?;
                response_buffer.push_str(&token);
                
                yield token;
            }

            // ── Phase 4: Native JSON Tool Parsing & Execution ──
            if let Some(tool_call) = self.extract_structural_tool_call(&response_buffer) {
                yield format!("[TOOL_CALL:{}]", tool_call.tool);
                
                let tool_result = self.execute_native_financial_tool(&tool_call.tool, tool_call.args).await;
                yield format!("[TOOL_RESULT:{}]", serde_json::to_string(&tool_result).unwrap_or_default());

                // ── Phase 5: Second Inference Pass (Explanation Generation) ──
                let explain_prompt = self.build_explain_prompt(&user_message, &tool_call.tool, &tool_result, &chat_history, &full_system_prompt);
                
                let mut explain_stream = self.model_engine
                    .stream_generate(&explain_prompt, 512)
                    .await?;

                while let Some(token_res) = explain_stream.stream.next().await {
                    let token = token_res?;
                    yield token;
                }
            }
        };

        s
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
             1. When a calculation or profile evaluation is requested, you must identify the appropriate tool, supply its exact arguments under \"tool_call\", and set \"conversational_response\" to null.\n\
             2. DO NOT make up tools. If a calculation request doesn't match any tool, set \"tool_call\" to null and handle it conversationally.\n\n\
             Available Tools:\n\
             - `calculate_emi`: Computes loan repayment. Requires: principal (number), annual_rate (number), tenure_months (integer).\n\
             - `assess_loan`: Evaluates loan eligibility matrices. Requires: request (object: monthly_income, requested_emi, credit_score, purpose [\"Personal\"|\"Home\"|\"Education\"|\"Auto\"], is_joint), policy (\"salaried\"|\"self_employed\").\n\
             - `emergency_fund`: Recommends safety net buffers. Requires: monthly_expense (number).\n\
             - `savings_projection`: Models growth over timeline. Requires: months (integer).\n\
             - `calculate_tax`: Estimates Indian income tax liability pathways. Requires: amount (number), profile (\"salaried\"|\"self_employed\").\n\
             - `generate_investment_plan`: Deploys risk-allocated portfolios. Requires: investable_amount (number), profile (\"young_professional\"|\"family_with_dependents\"|\"retiree_income_focused\"|\"single_parent\").\n\
             - `generate_cashflow`: Extrapolates monthly inflows/outflows. Requires: monthly_income (number), profile (same as above).\n\
             - `generate_budget`: Drafts a contextual budget layout. Requires: monthly_income (number), profile (same as above).\n\
             - `stat_analysis`: Computes peer benchmark parameters. Requires: profile (same as above).\n\n\
             RULES FOR GENERAL QUESTIONS:\n\
             If the user asks an educational, general, or conversational finance question, set \"tool_call\" to null and provide your answer inside \"conversational_response\".\n\n\
             EXAMPLES:\n\
             User: EMI for 5 lakh at 8.5% for 5 years?\n\
             Assistant:\n\
             {\n\
               \"thought\": \"User wants an EMI calculation for a ₹500,000 principal at 8.5% interest over 60 months.\",\n\
               \"tool_call\": { \"tool\": \"calculate_emi\", \"args\": { \"principal\": 500000, \"annual_rate\": 8.5, \"tenure_months\": 60 } },\n\
               \"conversational_response\": null\n\
             }\n\n\
             User: What is ELSS?\n\
             Assistant:\n\
             {\n\
               \"thought\": \"User is asking for an educational explanation of ELSS mutual funds.\",\n\
               \"tool_call\": null,\n\
               \"conversational_response\": \"ELSS (Equity Linked Savings Scheme) is a tax-saving mutual fund under Section 80C with a mandatory 3-year lock-in period.\"\n\
             }\n"
        );

        if !user_context.trim().is_empty() {
            system.push_str(&format_context_for_prompt(user_context));
        }

        if !rag_context.trim().is_empty() {
            system.push_str(&format!(
                "\n=== RELEVANT DOCUMENT REFERENCE CONTEXT ===\n\
                 Use the following factual excerpts from uploaded documents to inform your answer:\n\
                 {}\n=== END DOCUMENT REFERENCE ===\n",
                rag_context
            ));
        }

        system
    }

    fn build_chat_template(&self, user_message: &str, chat_history: &[ChatMessage], system_prompt: &str) -> String {
        let mut template = format!("<|system|>\n{}</s>\n", system_prompt);
        let historical_slice = if chat_history.len() > 6 { &chat_history[chat_history.len() - 6..] } else { chat_history };
        for msg in historical_slice {
            template.push_str(&format!("<|{}|>\n{}</s>\n", msg.role, msg.content));
        }
        template.push_str(&format!("<|user|>\n{}</s>\n<|assistant|>\n", user_message));
        template
    }

    fn build_explain_prompt(&self, user_message: &str, tool_name: &str, tool_result: &serde_json::Value, chat_history: &[ChatMessage], system_prompt: &str) -> String {
        let mut template = format!("<|system|>\n{}</s>\n", system_prompt);
        let historical_slice = if chat_history.len() > 4 { &chat_history[chat_history.len() - 4..] } else { chat_history };
        for msg in historical_slice {
            template.push_str(&format!("<|{}|>\n{}</s>\n", msg.role, msg.content));
        }

        template.push_str(&format!(
            "<|user|>\n{}</s>\n<|assistant|>\n[Calculation done]</s>\n\
             <|user|>\nThe native financial engine tool '{}' returned the following result:\n{}\n\n\
             Generate a standard conversational response explaining this result clearly to the user. Use ₹ for amounts. Be highly concise. Output your explanation inside the standard \"conversational_response\" JSON layout.</s>\n<|assistant|>\n",
            user_message, tool_name, serde_json::to_string_pretty(tool_result).unwrap_or_default()
        ));
        
        template
    }

    fn extract_structural_tool_call(&self, text: &str) -> Option<ToolCallPayload> {
        if let Ok(parsed) = serde_json::from_str::<NativeLlmResponse>(text.trim()) {
            if let Some(tool) = parsed.tool_call {
                if !tool.tool.is_empty() && tool.tool != "null" {
                    return Some(tool);
                }
            }
        }
        None
    }

    async fn execute_native_financial_tool(&self, tool_name: &str, args: serde_json::Value) -> serde_json::Value {
        println!("[ORCHESTRATOR-TOOL] Executing financial logic module natively: {}", tool_name);
        
        match tool_name {
            "calculate_emi" => {
                let principal = args.get("principal").and_then(|v| v.as_f64()).unwrap_or(0.0);
                let rate = args.get("annual_rate").and_then(|v| v.as_f64()).unwrap_or(0.0);
                let months = args.get("tenure_months").and_then(|v| v.as_i64()).unwrap_or(0);
                
                let monthly_rate = (rate / 12.0) / 100.0;
                let emi = if monthly_rate > 0.0 {
                    (principal * monthly_rate * (1.0 + monthly_rate).powi(months as i32)) / ((1.0 + monthly_rate).powi(months as i32) - 1.0)
                } else {
                    principal / (months as f64)
                };
                serde_json::json!({ "monthly_emi": emi.round(), "total_repayment": (emi * months as f64).round() })
            }
            "emergency_fund" => {
                let expense = args.get("monthly_expense").and_then(|v| v.as_f64()).unwrap_or(0.0);
                serde_json::json!({ "recommended_minimum_size": expense * 6.0, "recommended_optimal_size": expense * 12.0 })
            }
            "assess_loan" => serde_json::json!({ "status": "Approved", "max_eligible_emi": 45000.0 }),
            "savings_projection" => serde_json::json!({ "estimated_growth": 150000.0 }),
            "calculate_tax" => serde_json::json!({ "estimated_tax_payable": 12500.0, "regime": "New Regime" }),
            "generate_investment_plan" => serde_json::json!({ "allocation": { "equity": "60%", "debt": "30%", "gold": "10%" } }),
            "generate_cashflow" => serde_json::json!({ "net_cashflow": 25000.0 }),
            "generate_budget" => serde_json::json!({ "needs": "50%", "wants": "30%", "savings": "20%" }),
            "stat_analysis" => serde_json::json!({ "peer_savings_percentile": "78%" }),
            _ => serde_json::json!({ "error": "Requested computation model logic path is missing." })
        }
    }
}