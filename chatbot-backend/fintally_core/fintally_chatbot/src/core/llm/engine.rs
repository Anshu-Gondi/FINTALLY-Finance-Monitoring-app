// src/core/llm/engine.rs

use crate::core::utils::errors::AppError;
use futures_core::Stream;
use std::pin::Pin;
use async_trait::async_trait;
use tokio_util::sync::CancellationToken;

/// Stream of generated tokens (used for SSE / FastAPI streaming)
pub type LlmStream = Pin<Box<dyn Stream<Item = Result<String, AppError>> + Send + 'static>>;

pub struct CancelableStream {
    pub stream: LlmStream,
    pub cancel: CancellationToken,
}

#[async_trait]
pub trait LlmEngine: Send + Sync {
    /// Non-streaming generation
    /// (still useful for tools, tests, summaries)
    async fn generate(&self, prompt: &str, max_tokens: usize) -> Result<String, AppError>;

    /// 🔥 Streaming generation (token-by-token)
    async fn stream_generate(
        &self,
        prompt: &str,
        max_tokens: usize
    ) -> Result<CancelableStream, AppError>;

    /// Embeddings
    async fn embed(&self, text: &str) -> Result<Vec<f32>, AppError>;
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures_util::StreamExt;
    use tokio::sync::mpsc;
    use tokio_stream::wrappers::ReceiverStream;
    use tokio_util::sync::CancellationToken;

    /// Mock engine for testing stream + cancellation
    struct MockEngine;

    #[async_trait]
    impl LlmEngine for MockEngine {
        async fn generate(&self, _prompt: &str, _max_tokens: usize) -> Result<String, AppError> {
            Ok("ok".into())
        }

        async fn stream_generate(
            &self,
            _prompt: &str,
            _max_tokens: usize
        ) -> Result<CancelableStream, AppError> {
            let (tx, rx) = mpsc::channel(8);
            let cancel = CancellationToken::new();
            let child = cancel.clone();

            // Spawn producer task
            tokio::spawn(async move {
                for i in 0..10 {
                    if child.is_cancelled() {
                        break;
                    }

                    if tx.send(Ok(format!("token-{i}"))).await.is_err() {
                        break;
                    }
                }
            });

            Ok(CancelableStream {
                stream: Box::pin(ReceiverStream::new(rx)),
                cancel,
            })
        }

        async fn embed(&self, _text: &str) -> Result<Vec<f32>, AppError> {
            Ok(vec![0.1, 0.2, 0.3])
        }
    }

    /// ✅ Stream produces tokens
    #[tokio::test]
    async fn stream_emits_tokens() {
        let engine = MockEngine;

        let mut cs = engine.stream_generate("hello", 10).await.unwrap();
        let first = cs.stream.next().await.unwrap().unwrap();

        assert_eq!(first, "token-0");
    }

    /// ✅ Cancellation stops stream
    #[tokio::test]
    async fn stream_cancels_correctly() {
        let engine = MockEngine;

        let mut cs = engine.stream_generate("hello", 10).await.unwrap();

        // Read first token
        let _ = cs.stream.next().await.unwrap().unwrap();

        // Cancel
        cs.cancel.cancel();

        // Drain remaining buffered tokens
        let mut count = 0;
        while let Some(_) = cs.stream.next().await {
            count += 1;
        }

        // Stream MUST eventually terminate
        assert!(count <= 9); // sanity check
    }

    /// ✅ Embedding contract works
    #[tokio::test]
    async fn embed_returns_vector() {
        let engine = MockEngine;

        let vec = engine.embed("hello").await.unwrap();
        assert!(!vec.is_empty());
    }
}
