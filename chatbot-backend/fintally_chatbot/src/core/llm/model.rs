use crate::core::llm::engine::LlmEngine;
use crate::core::llm::prompt::Prompt;
use crate::core::utils::errors::AppError;

pub struct LLM {
    engine: Box<dyn LlmEngine>,
    pub model_name: String,
    pub max_tokens: usize,
}

impl LLM {
    pub fn new(
        engine: Box<dyn LlmEngine>,
        model_name: &str,
        max_tokens: usize,
    ) -> Self {
        Self {
            engine,
            model_name: model_name.to_string(),
            max_tokens: max_tokens.min(512),
        }
    }

    pub fn generate_text(
        &self,
        prompt_text: &str,
        context: Option<&str>,
    ) -> Result<String, AppError> {
        let full_prompt = Prompt::build(prompt_text, context)?;
        self.engine.generate(&full_prompt, self.max_tokens)
    }

    pub fn embed_text(&self, text: &str) -> Result<Vec<f32>, AppError> {
        self.engine.embed(text)
    }
}
