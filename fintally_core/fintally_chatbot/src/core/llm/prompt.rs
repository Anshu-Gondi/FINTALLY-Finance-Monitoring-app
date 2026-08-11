// src/core/llm/prompt.rs
//
// FIXED: Removed the keyword filter that rejected "calculate" / "emi" prompts.
// That filter was designed for a raw LLM endpoint, but a chatbot assistant
// MUST accept those words — they are the primary use case.
//
// The separation of concerns is:
//   - Planner (tools.rs) handles structured numeric execution
//   - prompt.rs only validates and formats text for the LLM

use crate::core::utils::errors::AppError;

/// Prompt builder for LLM requests
pub struct Prompt;

impl Prompt {
    /// Build a text-based prompt for the LLM.
    /// Validates non-empty input and combines context + prompt.
    pub fn build(prompt_text: &str, context: Option<&str>) -> Result<String, AppError> {
        if prompt_text.trim().is_empty() {
            return Err(AppError::InvalidInput("Prompt cannot be empty.".into()));
        }

        // Sanity: reject absurdly long prompts (> 8000 chars)
        // to protect context window budget
        if prompt_text.len() > 8000 {
            return Err(AppError::InvalidInput(
                "Prompt exceeds maximum allowed length.".into(),
            ));
        }

        let mut full_prompt = String::new();
        if let Some(ctx) = context {
            if !ctx.trim().is_empty() {
                full_prompt.push_str(ctx);
                full_prompt.push_str("\n\n");
            }
        }
        full_prompt.push_str(prompt_text);

        Ok(full_prompt)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_prompt_is_rejected() {
        let result = Prompt::build("", None);
        assert!(matches!(result, Err(AppError::InvalidInput(_))));
    }

    #[test]
    fn whitespace_only_prompt_is_rejected() {
        let result = Prompt::build("   \n\t  ", None);
        assert!(matches!(result, Err(AppError::InvalidInput(_))));
    }

    #[test]
    fn calculate_emi_prompt_is_now_accepted() {
        // This MUST work — it is the primary chatbot use case
        let result = Prompt::build("calculate my emi for 5 lakh loan", None);
        assert!(result.is_ok());
    }

    #[test]
    fn oversized_prompt_is_rejected() {
        let huge = "x".repeat(9000);
        let result = Prompt::build(&huge, None);
        assert!(matches!(result, Err(AppError::InvalidInput(_))));
    }

    #[test]
    fn simple_prompt_without_context_passes() {
        let result = Prompt::build("Explain budgeting", None).unwrap();
        assert_eq!(result, "Explain budgeting");
    }

    #[test]
    fn prompt_with_context_is_combined_correctly() {
        let context = "You are a financial assistant.";
        let prompt = "Explain budgeting basics.";
        let result = Prompt::build(prompt, Some(context)).unwrap();
        assert_eq!(result, "You are a financial assistant.\n\nExplain budgeting basics.");
    }

    #[test]
    fn empty_context_is_ignored() {
        let result = Prompt::build("Hello", Some("   ")).unwrap();
        assert_eq!(result, "Hello");
    }

    #[test]
    fn context_only_does_not_bypass_empty_prompt_check() {
        let result = Prompt::build("", Some("System context"));
        assert!(matches!(result, Err(AppError::InvalidInput(_))));
    }
}
