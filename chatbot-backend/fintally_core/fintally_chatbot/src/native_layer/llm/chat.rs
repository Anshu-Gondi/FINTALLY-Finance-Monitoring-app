use std::sync::Arc;
use crate::core::llm::model::LLM;
use crate::core::llm::native_engine::NativeLlamaEngine;

/// Factory function initializing the model using Mmap assets from workspace specifications
pub fn create_llm(max_tokens: usize) -> Arc<LLM> {
    let vault_path = "llm_models/chat/qwen_safetensors_output";
    
    let engine = match NativeLlamaEngine::load_from_vault(vault_path) {
        Ok(loaded_engine) => Box::new(loaded_engine),
        Err(e) => {
            eprintln!("[FATAL CRITICAL LLM ERROR] Failed to load local Qwen Safetensors model assets: {e}");
            panic!("Cannot initialize local chatbot engine dependencies safely: {e}");
        }
    };

    let llm = LLM::new(engine, "qwen-safetensors", max_tokens);
    Arc::new(llm)
}