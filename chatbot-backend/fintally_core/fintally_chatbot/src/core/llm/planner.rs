// src/core/llm/planner.rs

use serde_json::Value;
use crate::core::llm::tools::{execute_tool_async, ToolName};
use crate::core::utils::errors::AppError;
use std::str::FromStr;

/// Async Planner for orchestrating LLM tool execution
pub struct Planner;

impl Planner {
    /// Execute any tool asynchronously
    pub async fn execute(tool_name: &str, args: Value) -> Result<Value, AppError> {
        // Parse the tool name
        let _tool = ToolName::from_str(tool_name)?;

        // Call the async tool executor (handles all tools dynamically)
        execute_tool_async(tool_name, args).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use tokio;

    #[tokio::test]
    async fn planner_calculate_emi() {
        let args = json!({
            "principal": 1_00_000,
            "annual_rate": 12.0,
            "tenure_months": 12
        });

        let result = Planner::execute("calculate_emi", args).await.unwrap();
        assert!(result.get("emi").is_some());
    }

    #[tokio::test]
    async fn planner_assess_loan() {
        use crate::core::types::*;
        let request = LoanRequest {
            monthly_income: 80_000.0,
            existing_emi: 10_000.0,
            requested_emi: 20_000.0,
            credit_score: 750,
            purpose: LoanPurpose::Personal,
            is_joint: false,
        };
        let policy = LoanPolicy::salaried();

        let args = json!({
            "request": request,
            "policy": policy
        });

        let result = Planner::execute("assess_loan", args).await.unwrap();
        assert!(result.get("approved").is_some());
    }

    #[tokio::test]
    async fn planner_unknown_tool() {
        let args = json!({});
        let result = Planner::execute("unknown_tool", args).await;
        assert!(result.is_err());
        // Match the exact error message from ToolName::from_str
        assert!(result.unwrap_err().to_string().contains("Unknown tool"));
    }
}
