use crate::core::llm::engine::{ LlmEngine, CancelableStream };
use crate::core::llm::prompt::Prompt;
use crate::core::utils::errors::AppError;

pub struct LLM {
    engine: Box<dyn LlmEngine>,
    pub model_name: String,
    pub max_tokens: usize,
}

impl LLM {
    pub fn new(engine: Box<dyn LlmEngine>, model_name: &str, max_tokens: usize) -> Self {
        Self {
            engine,
            model_name: model_name.to_string(),
            max_tokens: max_tokens.min(512),
        }
    }

    // Non-streaming
    pub async fn generate_text(
        &self,
        prompt_text: &str,
        context: Option<&str>
    ) -> Result<String, AppError> {
        let full_prompt = Prompt::build(prompt_text, context)?;
        self.engine.generate(&full_prompt, self.max_tokens).await
    }

    // 🔥 Streaming
    pub async fn stream_text(
        &self,
        prompt_text: &str,
        context: Option<&str>
    ) -> Result<CancelableStream, AppError> {
        let full_prompt = Prompt::build(prompt_text, context)?;
        self.engine.stream_generate(&full_prompt, self.max_tokens).await
    }

    pub async fn embed_text(&self, text: &str) -> Result<Vec<f32>, AppError> {
        self.engine.embed(text).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use async_trait::async_trait;
    use futures_core::Stream;
    use std::pin::Pin;
    use tokio_util::sync::CancellationToken;
    use tokio_stream::iter;

    /// Simple mock engine
    struct MockEngine;

    impl MockEngine {
        fn new() -> Self {
            Self 
        }
    }

    #[async_trait]
    impl LlmEngine for MockEngine {
        async fn generate(&self, prompt: &str, max_tokens: usize) -> Result<String, AppError> {
            Ok(format!("GEN:{}:{}", prompt, max_tokens))
        }

        async fn stream_generate(
            &self,
            prompt: &str,
            max_tokens: usize
        ) -> Result<CancelableStream, AppError> {
            let stream: Pin<Box<dyn Stream<Item = Result<String, AppError>> + Send>> = Box::pin(
                iter(vec![Ok(format!("STREAM:{}:{}", prompt, max_tokens))])
            );

            Ok(CancelableStream {
                stream,
                cancel: CancellationToken::new(),
            })
        }

        async fn embed(&self, _text: &str) -> Result<Vec<f32>, AppError> {
            Ok(vec![1.0, 2.0, 3.0])
        }
    }

    #[tokio::test]
    async fn invalid_prompt_is_rejected() {
        let engine = Box::new(MockEngine::new());
        let llm = LLM::new(engine, "test", 128);

        let result = llm.generate_text("", None).await;

        assert!(matches!(result, Err(AppError::InvalidInput(_))));
    }

    #[tokio::test]
    async fn max_tokens_is_clamped() {
        let engine = Box::new(MockEngine::new());
        let llm = LLM::new(engine, "test", 10_000);

        let result = llm.generate_text("Hello", None).await.unwrap();

        assert!(result.ends_with(":512"));
    }

    #[tokio::test]
    async fn streaming_returns_cancelable_stream() {
        let engine = Box::new(MockEngine::new());
        let llm = LLM::new(engine, "test", 64);

        let cancelable = llm.stream_text("Hi", None).await.unwrap();

        let mut stream = cancelable.stream;
        let item = stream.next().await.unwrap().unwrap();

        assert_eq!(item, "STREAM:Hi:64");
    }

    #[tokio::test]
    async fn embed_text_is_forwarded() {
        let engine = Box::new(MockEngine::new());
        let llm = LLM::new(engine, "test", 128);

        let vec = llm.embed_text("test").await.unwrap();

        assert_eq!(vec, vec![1.0, 2.0, 3.0]);
    }
}
