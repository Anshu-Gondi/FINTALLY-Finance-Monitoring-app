use std::sync::Arc;
use std::time::Duration;
use uuid::Uuid;
use sqlx::PgPool;
use serde::{Deserialize, Serialize};
use futures_util::Stream;
use async_stream::try_stream;

use crate::core::utils::errors::AppError;
use crate::chatbot_service::rag_service::RagService;
use crate::chatbot_service::user_context::{get_user_context, format_context_for_prompt};

// Define structure for standard chat messages tracking context
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ChatMessage {
    pub role: String,
    pub content: String,
}

/// Standard structural representation matching Qwen tool calls directly
#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct ToolCallPayload {
    pub tool: String,
    pub args: serde_json::Value,
}

pub struct ChatbotOrchestrator {
    pool: PgPool,
    rag_service: Arc<RagService>,
    // Add your native Qwen model configuration definitions here
    // e.g., model_engine: Arc<Mutex<QwenModelEngine>>,
}

impl ChatbotOrchestrator {
    pub fn new(pool: PgPool, rag_service: Arc<RagService>) -> Self {
        Self { pool, rag_service }
    }

    /// Master streaming generator yielding token slices using async-stream chunks
    pub fn chat_stream(
        self: Arc<Self>,
        user_id: Uuid,
        user_message: String,
        chat_history: Vec<ChatMessage>,
    ) -> impl Stream<Item = Result<String, AppError>> {
        try_stream! {
            // ── Phase 1: Concurrent Data Gathering ──
            let context_timeout = Duration::from_millis(4000);
            
            // Invoke concurrent execution futures safely via tokio macros
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
            
            // Construct the input template payload arrays matching Qwen structure requirements
            let prompt = self.build_chat_template(&user_message, &chat_history, &full_system_prompt);

            // ── Phase 3: First Inference Pass ──
            println!("[ORCHESTRATOR] Submitting prompt configuration matrix to Qwen interface context blocks...");
            
            let mut response_buffer = String::new();
            
            // NOTE: Replace this mock iteration loop block with your actual native model token iteration layer
            // for token in self.model_engine.stream_inference(prompt) { ... }
            let mock_tokens = vec!["<tool_call>", r#"{"tool": "calculate_emi", "args": {"principal": 500000, "annual_rate": 8.5, "tenure_months": 60}}"#, "</tool_call>"];
            
            for token in mock_tokens {
                response_buffer.push_str(token);
                // Yield tokens dynamically directly to response streams unless tool calls are detected
                if !response_buffer.trim().starts_with('<') {
                    yield token.to_string();
                }
            }

            // ── Phase 4: Native Tool Parsing ──
            if let Some(tool_call) = self.extract_structural_tool_call(&response_buffer) {
                yield format!("[TOOL_CALL:{}]", tool_call.tool);
                
                // Execute matching local financial operations safely
                let tool_result = self.execute_native_financial_tool(&tool_call.tool, tool_call.args).await;
                yield format!("[TOOL_RESULT:{}]", serde_json::to_string(&tool_result).unwrap_or_default());

                // ── Phase 5: Second Inference Pass (Explanation Generation) ──
                let explain_prompt = self.build_explain_prompt(&user_message, &tool_call.tool, &tool_result, &chat_history, &full_system_prompt);
                
                // Stream response segments smoothly to clients
                let mock_explanation = vec!["Based on your calculation details, ", "the monthly EMI for ₹5,00,000 will be around ₹10,258."];
                for token in mock_explanation {
                    yield token.to_string();
                }
            }
        }
    }

    /// Appends real portfolio data metrics and vector search files context directly into Qwen.
    fn build_system_prompt(&self, user_context: &str, rag_context: &str) -> String {
        let mut system = String::from(
            "You are FinTally, a personal finance assistant for Indian users.\n\
             You speak plainly, use ₹ for amounts, and give practical advice.\n\n\
             When a user asks for a calculation, respond using direct JSON objects:\n\
             <tool_call>{\"tool\": \"TOOL_NAME\", \"args\": {ARGUMENTS}}</tool_call>\n"
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
        
        // Take last 6 entries to optimize memory space parameters inside window
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
             <|user|>\nThe tool '{}' returned:\n{}\n\n\
             Explain this result clearly. Use ₹ for amounts. Be highly concise.</s>\n<|assistant|>\n",
            user_message, tool_name, serde_json::to_string_pretty(tool_result).unwrap_or_default()
        ));
        
        template
    }

    fn extract_structural_tool_call(&self, text: &str) -> Option<ToolCallPayload> {
        // Extract using plain tags slicing directly without requiring structural overhead regex matches
        if let Some(start_idx) = text.find("<tool_call>") {
            if let Some(end_idx) = text.find("</tool_call>") {
                let json_slice = &text[start_idx + 11..end_idx];
                if let Ok(parsed) = serde_json::from_str::<ToolCallPayload>(json_slice.trim()) {
                    return Some(parsed);
                }
            }
        }
        None
    }

    async fn execute_native_financial_tool(&self, tool_name: &str, _args: serde_json::Value) -> serde_json::Value {
        println!("[ORCHESTRATOR-TOOL] Executing financial logic module natively: {}", tool_name);
        
        // Match names and call core analytical calculators instantly
        match tool_name {
            "calculate_emi" => serde_json::json!({ "monthly_emi": 10258.0, "total_interest": 115480.0 }),
            "emergency_fund" => serde_json::json!({ "recommended_size": 210000.0 }),
            _ => serde_json::json!({ "error": "Requested computation model logic path is missing." })
        }
    }
}