// src/core/llm/engine.rs
use crate::core::utils::errors::AppError;

#[async_trait::async_trait]
pub trait LlmEngine: Send + Sync {
    async fn generate(&self, prompt: &str, max_tokens: usize)
        -> Result<String, AppError>;

    async fn embed(&self, text: &str)
        -> Result<Vec<f32>, AppError>;
}
