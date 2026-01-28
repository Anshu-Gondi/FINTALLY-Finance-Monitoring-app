// src/core/llm/engine.rs

use crate::core::utils::errors::AppError;
use futures_core::Stream;
use std::pin::Pin;
use async_trait::async_trait;
use tokio_util::sync::CancellationToken;

/// Stream of generated tokens (used for SSE / FastAPI streaming)
pub type LlmStream =
    Pin<Box<dyn Stream<Item = Result<String, AppError>> + Send + 'static>>;

pub struct CancelableStream {
    pub stream: LlmStream,
    pub cancel: CancellationToken,
}

#[async_trait]
pub trait LlmEngine: Send + Sync {
    /// Non-streaming generation
    /// (still useful for tools, tests, summaries)
    async fn generate(
        &self,
        prompt: &str,
        max_tokens: usize,
    ) -> Result<String, AppError>;

    /// 🔥 Streaming generation (token-by-token)
    async fn stream_generate(
        &self,
        prompt: &str,
        max_tokens: usize,
    ) -> Result<CancelableStream, AppError>;

    /// Embeddings
    async fn embed(
        &self,
        text: &str,
    ) -> Result<Vec<f32>, AppError>;
}
