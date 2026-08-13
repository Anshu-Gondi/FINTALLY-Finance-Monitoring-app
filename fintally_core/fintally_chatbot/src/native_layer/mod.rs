pub mod llm;
pub mod finance;
pub mod rag;
pub mod vision;

pub use vision::OcrService;

use crate::native_layer::llm::chat::create_llm;

/// Optional: A clean aggregate structural configuration struct for initializing
/// the entire chatbot workspace dependencies in a single step inside your web server.
pub struct ChatbotServices {
    pub llm: std::sync::Arc<crate::core::llm::model::LLM>,
    pub rag: std::sync::Arc<tokio::sync::RwLock<rag::RagEngine>>,
}

impl ChatbotServices {
    pub fn init(
        _model_name: &str,
        max_tokens: usize,
        model_vault_path: &str,
    ) -> Result<Self, crate::core::rag::errors::RagError> {
        let llm = create_llm(max_tokens);

        let rag_engine = rag::RagEngine::new(
            model_vault_path,
            512,  // chunk_size
            64,   // chunk_overlap
            384,  // dimensions
            0.5,  // alpha
            5,    // default_top_k
            None, // index_path
            None, // chunks_path
        )?;

        Ok(Self {
            llm,
            rag: std::sync::Arc::new(tokio::sync::RwLock::new(rag_engine)),
        })
    }
}
