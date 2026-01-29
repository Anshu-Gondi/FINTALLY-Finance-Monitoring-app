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

#[cfg(test)]
mod tests {
    use super::*;
    use async_trait::async_trait;

    /// Mock engine for embedding tests
    struct MockEngine;

    #[async_trait]
    impl LlmEngine for MockEngine {
        async fn generate(
            &self,
            _prompt: &str,
            _max_tokens: usize,
        ) -> Result<String, AppError> {
            unreachable!("generate should not be called in embedding tests")
        }

        async fn stream_generate(
            &self,
            _prompt: &str,
            _max_tokens: usize,
        ) -> Result<crate::core::llm::engine::CancelableStream, AppError> {
            unreachable!("stream_generate should not be called in embedding tests")
        }

        async fn embed(&self, _text: &str) -> Result<Vec<f32>, AppError> {
            Ok(vec![0.1, 0.2, 0.3])
        }
    }

    /// ❌ Empty input rejected
    #[tokio::test]
    async fn embedding_rejects_empty_text() {
        let engine = Arc::new(MockEngine);
        let embedding = Embedding::new(engine);

        let err = embedding.generate("").await.unwrap_err();
        assert!(matches!(err, AppError::InvalidInput(_)));
    }

    /// ❌ Numeric-only input rejected
    #[tokio::test]
    async fn embedding_rejects_numeric_only_text() {
        let engine = Arc::new(MockEngine);
        let embedding = Embedding::new(engine);

        let err = embedding.generate("123 456 789").await.unwrap_err();
        assert!(matches!(err, AppError::InvalidInput(_)));
    }

    /// ❌ Overly long input rejected
    #[tokio::test]
    async fn embedding_rejects_too_long_text() {
        let engine = Arc::new(MockEngine);
        let embedding = Embedding::new(engine);

        let long_text = "a".repeat(8_001);
        let err = embedding.generate(&long_text).await.unwrap_err();

        assert!(matches!(err, AppError::InvalidInput(_)));
    }

    /// ✅ Valid input returns embedding
    #[tokio::test]
    async fn embedding_returns_vector_for_valid_text() {
        let engine = Arc::new(MockEngine);
        let embedding = Embedding::new(engine);

        let vec = embedding.generate("hello world").await.unwrap();
        assert_eq!(vec, vec![0.1, 0.2, 0.3]);
    }
}
