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


#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::utils::errors::AppError;

    #[test]
    fn empty_prompt_is_rejected() {
        let result = Prompt::build("", None);

        match result {
            Err(AppError::InvalidInput(msg)) => {
                assert!(msg.contains("Prompt cannot be empty"));
            }
            _ => panic!("Empty prompt should return InvalidInput error"),
        }
    }

    #[test]
    fn whitespace_only_prompt_is_rejected() {
        let result = Prompt::build("   \n\t  ", None);

        assert!(matches!(
            result,
            Err(AppError::InvalidInput(_))
        ));
    }

    #[test]
    fn numeric_or_calculation_prompt_is_rejected() {
        let result = Prompt::build("calculate my emi", None);

        match result {
            Err(AppError::InvalidInput(msg)) => {
                assert!(msg.contains("Planner tools"));
            }
            _ => panic!("Calculation prompt should be rejected"),
        }
    }

    #[test]
    fn simple_prompt_without_context_passes() {
        let prompt = "Explain Rust ownership";

        let result = Prompt::build(prompt, None).unwrap();

        assert_eq!(result, prompt);
    }

    #[test]
    fn prompt_with_context_is_combined_correctly() {
        let context = "You are a financial assistant.";
        let prompt = "Explain budgeting basics.";

        let result = Prompt::build(prompt, Some(context)).unwrap();

        assert_eq!(
            result,
            "You are a financial assistant.\n\nExplain budgeting basics."
        );
    }

    #[test]
    fn context_only_does_not_bypass_empty_prompt_check() {
        let context = "System context";

        let result = Prompt::build("", Some(context));

        assert!(matches!(
            result,
            Err(AppError::InvalidInput(_))
        ));
    }
}
