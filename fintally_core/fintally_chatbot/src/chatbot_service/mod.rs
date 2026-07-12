pub mod rag_service;
pub mod user_context;
pub mod chatbot_orchestrator; // ◄─ ADD THIS LINE

pub use rag_service::RagService;
pub use user_context::{get_user_context, format_context_for_prompt};
pub use chatbot_orchestrator::{ChatbotOrchestrator, ChatMessage}; // ◄─ ADD THIS LINE