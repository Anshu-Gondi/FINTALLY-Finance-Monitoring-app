#![recursion_limit = "512"]

pub mod core;
pub mod native_layer;
pub mod chatbot_service;

// Re-export major native layers for seamless cross-crate dependency declarations
pub use native_layer::finance::execute_tool;
pub use native_layer::llm::chat::create_llm;
pub use native_layer::rag::engine::RagEngine;
