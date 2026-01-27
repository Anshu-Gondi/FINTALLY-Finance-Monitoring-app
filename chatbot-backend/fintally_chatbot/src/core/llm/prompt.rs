use serde_json::Value;
use crate::core::utils::errors::AppError;

/// Prompt builder for LLM requests
pub struct Prompt;

impl Prompt {
    /// Build a text-based prompt for the LLM
    /// Only accepts text instructions; numeric/math data is forbidden
    pub fn build(prompt_text: &str, context: Option<&str>) -> Result<String, AppError> {
        if prompt_text.trim().is_empty() {
            return Err(AppError::InvalidInput("Prompt cannot be empty.".into()));
        }

        // Basic sanitization: reject attempts to pass structured numeric input
        if prompt_text.contains("calculate") || prompt_text.contains("emi") {
            return Err(AppError::InvalidInput(
                "Numeric or structured calculation requests must use Planner tools.".into(),
            ));
        }

        let mut full_prompt = String::new();
        if let Some(ctx) = context {
            full_prompt.push_str(ctx);
            full_prompt.push_str("\n\n");
        }
        full_prompt.push_str(prompt_text);

        Ok(full_prompt)
    }
}
