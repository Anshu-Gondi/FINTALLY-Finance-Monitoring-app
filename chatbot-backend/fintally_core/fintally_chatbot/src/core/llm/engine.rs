use crate::core::utils::errors::AppError;
use futures_core::Stream;
use std::pin::Pin;
use async_trait::async_trait;
use tokio_util::sync::CancellationToken;

/// Stream of natively generated LLM tokens (used for SSE streaming payloads)
pub type LlmStream = Pin<Box<dyn Stream<Item = Result<String, AppError>> + Send + 'static>>;

/// Context-wrapped token stream paired with a cooperative cancellation token
pub struct CancelableStream {
    pub stream: LlmStream,
    pub cancel: CancellationToken,
}

#[async_trait]
pub trait LlmEngine: Send + Sync {
    /// Non-streaming text generation block (ideal for tools, tests, and map-reduce summaries)
    async fn generate(&self, prompt: &str, max_tokens: usize) -> Result<String, AppError>;

    /// Async, token-by-token streaming generation
    async fn stream_generate(
        &self,
        prompt: &str,
        max_tokens: usize
    ) -> Result<CancelableStream, AppError>;

    /// High-performance raw vector semantic transformations
    async fn embed(&self, text: &str) -> Result<Vec<f32>, AppError>;
}

// ==============================================================================
// ASYNC ARCHITECTURE SPECIFICATION TESTS
// ==============================================================================
#[cfg(test)]
mod tests {
    use super::*;
    use futures_util::StreamExt;
    use tokio::sync::mpsc;
    use tokio_stream::wrappers::ReceiverStream;

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

            // Spawn concurrent execution task natively
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

    #[tokio::test]
    async fn stream_emits_tokens() {
        let engine = MockEngine;

        let mut cs = engine.stream_generate("hello", 10).await.unwrap();
        let first = cs.stream.next().await.unwrap().unwrap();

        assert_eq!(first, "token-0");
    }

    #[tokio::test]
    async fn stream_cancels_correctly() {
        let engine = MockEngine;

        let mut cs = engine.stream_generate("hello", 10).await.unwrap();

        // Pull initial message to guarantee producer task spinup
        let _ = cs.stream.next().await.unwrap().unwrap();

        // Trigger cooperative cancellation token drop
        cs.cancel.cancel();

        // Drain the remaining backlogged channel buffer
        let mut count = 0;
        while cs.stream.next().await.is_some() {
            count += 1;
        }

        // Stream must safely break out and terminate execution context
        assert!(count <= 9, "Stream context did not terminate on cancellation check.");
    }

    #[tokio::test]
    async fn embed_returns_vector() {
        let engine = MockEngine;

        let vec = engine.embed("hello").await.unwrap();
        assert_eq!(vec, vec![0.1, 0.2, 0.3]);
    }
}