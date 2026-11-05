pub mod chatbot_orchestrator; // ◄─ ADD THIS LINE
pub mod creator_bio;
pub mod rag_service;
pub mod user_context;
pub mod vision_service;

pub use chatbot_orchestrator::{ChatMessage, ChatbotOrchestrator}; // ◄─ ADD THIS LINE
pub use creator_bio::CREATOR_BIO;
pub use rag_service::RagService;
pub use user_context::{format_context_for_prompt, get_user_context};
pub use vision_service::VisionChatbotService;
