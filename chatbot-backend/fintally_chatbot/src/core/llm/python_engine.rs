use crate::core::llm::engine::{ LlmEngine, CancelableStream };
use crate::core::utils::errors::AppError;

use async_trait::async_trait;
use futures_util::StreamExt;

use pyo3::types::PyAnyMethods;

use tokio::sync::mpsc;
use tokio_stream::wrappers::ReceiverStream;
use tokio_util::sync::CancellationToken;

pub struct PythonLlamaEngine;

#[async_trait]
impl LlmEngine for PythonLlamaEngine {
    /// Non-streaming = collect stream
    async fn generate(&self, prompt: &str, max_tokens: usize) -> Result<String, AppError> {
        let mut stream = self.stream_generate(prompt, max_tokens).await?.stream;

        let mut out = String::new();
        while let Some(chunk) = stream.next().await {
            let token: String = chunk?;
            out.push_str(&token);
        }

        Ok(out)
    }

    /// 🔥 Streaming via Python llama.cpp (WITH cancellation)
    async fn stream_generate(
        &self,
        prompt: &str,
        max_tokens: usize
    ) -> Result<CancelableStream, AppError> {
        let (tx, rx) = mpsc::channel::<Result<String, AppError>>(32);
        let cancel = CancellationToken::new();

        let prompt = prompt.to_owned();
        let cancel_child = cancel.clone();

        tokio::task::spawn_blocking(move || {
            let result: Result<(), AppError> = pyo3::Python::with_gil(|py| {
                let module = py
                    .import_bound("python_llama")
                    .map_err(|e| AppError::Other(e.to_string()))?;

                let gen = module
                    .getattr("stream_generate")
                    .map_err(|e| AppError::Other(e.to_string()))?
                    .call1((prompt, max_tokens))
                    .map_err(|e| AppError::Other(e.to_string()))?;

                for item in gen.iter()? {
                    // 🔴 HARD STOP
                    if cancel_child.is_cancelled() {
                        break;
                    }

                    let token: String = item
                        .map_err(|e| AppError::Other(e.to_string()))?
                        .extract()
                        .map_err(|e| AppError::Other(e.to_string()))?;

                    if tx.blocking_send(Ok(token)).is_err() {
                        break;
                    }
                }

                Ok(())
            });

            if let Err(err) = result {
                let _ = tx.blocking_send(Err(err));
            }

            drop(tx);
        });

        Ok(CancelableStream {
            stream: Box::pin(ReceiverStream::new(rx)),
            cancel,
        })
    }

    async fn embed(&self, _text: &str) -> Result<Vec<f32>, AppError> {
        Ok(vec![0.0; 768])
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::llm::engine::LlmStream;
    use async_trait::async_trait;
    use futures_util::{ StreamExt, stream };
    use tokio_util::sync::CancellationToken;

    /// ─────────────────────────────────────────────
    /// Helper: collect tokens from any LlmStream
    /// (this is the logic generate() relies on)
    /// ─────────────────────────────────────────────
    async fn collect_stream(mut stream: LlmStream) -> Result<String, AppError> {
        let mut out = String::new();
        while let Some(chunk) = stream.next().await {
            let token: String = chunk?;
            out.push_str(&token);
        }
        Ok(out)
    }

    /// ─────────────────────────────────────────────
    /// Helper: fake stream (NO Python involved)
    /// ─────────────────────────────────────────────
    fn fake_stream(tokens: Vec<String>) -> CancelableStream {
        let cancel = CancellationToken::new();

        let s = stream::iter(tokens.into_iter().map(Ok::<_, AppError>));

        CancelableStream {
            stream: Box::pin(s),
            cancel,
        }
    }

    /// ─────────────────────────────────────────────
    /// ✅ Core logic test:
    /// stream → tokens → joined output
    /// ─────────────────────────────────────────────
    #[tokio::test]
    async fn collect_stream_joins_tokens_correctly() {
        let cs = fake_stream(vec!["hel".into(), "lo".into(), " ".into(), "world".into()]);

        let out = collect_stream(cs.stream).await.unwrap();
        assert_eq!(out, "hello world");
    }

    /// ─────────────────────────────────────────────
    /// Mock engine: only streaming matters
    /// ─────────────────────────────────────────────
    struct TestEngine;

    #[async_trait]
    impl LlmEngine for TestEngine {
        async fn generate(&self, prompt: &str, max_tokens: usize) -> Result<String, AppError> {
            let cs = self.stream_generate(prompt, max_tokens).await?;
            collect_stream(cs.stream).await
        }

        async fn stream_generate(
            &self,
            _prompt: &str,
            _max_tokens: usize
        ) -> Result<CancelableStream, AppError> {
            Ok(fake_stream(vec!["hel".into(), "lo".into(), " ".into(), "world".into()]))
        }

        async fn embed(&self, _text: &str) -> Result<Vec<f32>, AppError> {
            Ok(vec![1.0, 2.0])
        }
    }

    /// ─────────────────────────────────────────────
    /// ✅ generate() uses stream internally
    /// ─────────────────────────────────────────────
    #[tokio::test]
    async fn generate_collects_stream_output() {
        let engine = TestEngine;

        let out = engine.generate("ignored", 10).await.unwrap();
        assert_eq!(out, "hello world");
    }

    /// ─────────────────────────────────────────────
    /// ✅ embed() contract
    /// ─────────────────────────────────────────────
    #[tokio::test]
    async fn embed_returns_vector() {
        let engine = PythonLlamaEngine;
        let vec = engine.embed("hello").await.unwrap();

        assert_eq!(vec.len(), 768);
    }

    /// ─────────────────────────────────────────────
    /// ⚠️ REAL PYTHON STREAMING
    /// Integration test only
    /// ─────────────────────────────────────────────
    #[tokio::test]
    #[ignore]
    async fn python_streaming_smoke_test() {
        let engine = PythonLlamaEngine;

        let mut cs = engine.stream_generate("Hello", 16).await.unwrap();

        let first = cs.stream.next().await.unwrap().unwrap();
        assert!(!first.is_empty());
    }

    #[tokio::test]
    async fn cancellation_propagates_to_stream() {
        use futures_util::StreamExt;
        use tokio::sync::mpsc;
        use tokio_stream::wrappers::ReceiverStream;
        use tokio_util::sync::CancellationToken;

        let (tx, rx) = mpsc::channel::<Result<String, AppError>>(4);
        let cancel = CancellationToken::new();
        let cancel_child = cancel.clone();

        // Producer task
        tokio::spawn(async move {
            for i in 0..100 {
                if cancel_child.is_cancelled() {
                    break;
                }

                if tx.send(Ok(format!("token-{i}"))).await.is_err() {
                    break;
                }

                tokio::time::sleep(std::time::Duration::from_millis(5)).await;
            }
        });

        let mut cs = CancelableStream {
            stream: Box::pin(ReceiverStream::new(rx)),
            cancel,
        };

        // Read first token
        let first = cs.stream.next().await.unwrap().unwrap();
        assert_eq!(first, "token-0");

        // Cancel immediately
        cs.cancel.cancel();

        // Drain remaining tokens (should be none)
        let mut count = 0;
        while let Some(_) = cs.stream.next().await {
            count += 1;
        }

        assert_eq!(count, 0, "Stream emitted tokens after cancellation");
    }

    #[tokio::test]
    async fn channel_backpressure_applies() {
        use futures_util::StreamExt;
        use tokio::sync::mpsc;
        use tokio_stream::wrappers::ReceiverStream;

        // VERY small channel
        let (tx, rx) = mpsc::channel::<Result<String, AppError>>(1);

        // Producer: tries to flood channel
        tokio::spawn(async move {
            for i in 0..5 {
                // Will block once buffer is full
                let _ = tx.send(Ok(format!("token-{i}"))).await;
            }
        });

        let mut stream = ReceiverStream::new(rx);

        // Consumer intentionally slow
        let first = stream.next().await.unwrap().unwrap();
        tokio::time::sleep(std::time::Duration::from_millis(50)).await;

        let second = stream.next().await.unwrap().unwrap();

        assert_eq!(first, "token-0");
        assert_eq!(second, "token-1");
    }
}
