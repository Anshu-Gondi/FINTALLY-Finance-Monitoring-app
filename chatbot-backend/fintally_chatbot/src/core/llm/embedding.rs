use crate::core::llm::engine::LlmEngine;
use crate::core::utils::errors::AppError;
use std::sync::Arc;

/// Embedding helper (async, engine-agnostic)
pub struct Embedding {
    engine: Arc<dyn LlmEngine>,
}

impl Embedding {
    pub fn new(engine: Arc<dyn LlmEngine>) -> Self {
        Self { engine }
    }

    pub async fn generate(&self, text: &str) -> Result<Vec<f32>, AppError> {
        if text.trim().is_empty() {
            return Err(AppError::InvalidInput(
                "Cannot generate embedding for empty text.".into(),
            ));
        }

        if text
            .chars()
            .all(|c| c.is_ascii_digit() || c.is_whitespace())
        {
            return Err(AppError::InvalidInput(
                "Numeric-only content must use Planner/Tools.".into(),
            ));
        }

        if text.len() > 8_000 {
            return Err(AppError::InvalidInput(
                "Text too long for embedding.".into(),
            ));
        }

        self.engine.embed(text).await
    }
}
