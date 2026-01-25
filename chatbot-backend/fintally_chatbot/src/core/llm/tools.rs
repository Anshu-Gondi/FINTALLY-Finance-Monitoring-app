// src/core/llm/tools.rs

use crate::core::llm::assistant;
use crate::core::utils::errors::AppError;
use serde_json::{json, Value};
use std::str::FromStr;

/// Enum of tool names
#[derive(Debug, Clone)]
pub enum ToolName {
    CalculateEmi,
    AssessLoan,
}

impl ToolName {
    pub fn as_str(&self) -> &'static str {
        match self {
            ToolName::CalculateEmi => "calculate_emi",
            ToolName::AssessLoan => "assess_loan",
        }
    }
}

impl FromStr for ToolName {
    type Err = AppError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "calculate_emi" => Ok(ToolName::CalculateEmi),
            "assess_loan" => Ok(ToolName::AssessLoan),
            _ => Err(AppError::InvalidInput(format!("Unknown tool: {}", s))),
        }
    }
}

/// Returns tool definitions for LLM function calling
pub fn tool_definitions() -> Vec<Value> {
    vec![
        json!({
            "type": "function",
            "function": {
                "name": ToolName::CalculateEmi.as_str(),
                "description": "Calculate monthly EMI based on principal, annual interest rate, and tenure in months.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "principal": { "type": "number" },
                        "annual_rate": { "type": "number" },
                        "tenure_months": { "type": "integer" }
                    },
                    "required": ["principal", "annual_rate", "tenure_months"]
                }
            }
        }),
        json!({
            "type": "function",
            "function": {
                "name": ToolName::AssessLoan.as_str(),
                "description": "Assess loan eligibility based on loan request and policy.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "request": {
                            "type": "object",
                            "properties": {
                                "monthly_income": { "type": "number" },
                                "existing_emi": { "type": "number" },
                                "requested_emi": { "type": "number" },
                                "credit_score": { "type": "integer" },
                                "purpose": { "type": "string" },
                                "is_joint": { "type": "boolean" }
                            },
                            "required": ["monthly_income", "requested_emi", "credit_score", "purpose"]
                        },
                        "policy": { "type": "object" }
                    },
                    "required": ["request", "policy"]
                }
            }
        }),
    ]
}

/// Executes a tool based on name and arguments (async version)
pub async fn execute_tool_async(tool_name: &str, arguments: Value) -> Result<Value, AppError> {
    let tool = ToolName::from_str(tool_name)?;
    match tool {
        ToolName::CalculateEmi => assistant::execute_calculate_emi_async(arguments).await,
        ToolName::AssessLoan => assistant::execute_assess_loan_async(arguments).await,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use tokio;

    #[tokio::test]
    async fn unknown_tool_returns_error() {
        let args = json!({});
        let result = execute_tool_async("not_a_tool", args).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Unknown tool"));
    }
}