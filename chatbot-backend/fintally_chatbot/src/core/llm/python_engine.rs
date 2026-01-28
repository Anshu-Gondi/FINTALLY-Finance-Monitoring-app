use crate::core::llm::engine::{LlmEngine, CancelableStream};
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
        let mut stream = self
            .stream_generate(prompt, max_tokens)
            .await?
            .stream;

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
        max_tokens: usize,
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
