// src/core/llm/tools.rs

use crate::core::llm::assistant;
use crate::core::utils::errors::AppError;
use serde_json::{ json, Value };
use std::str::FromStr;

/// Enum of tool names
#[derive(Debug, Clone)]
pub enum ToolName {
    CalculateEmi,
    AssessLoan,
    EmergencyFund,
    SavingsProjection,
    CalculateTax,
    InvestmentPlan,
    CashflowPlan,
    GenerateBudget,
    ProfileSimilarity,
    StatAnalysis,
}

impl ToolName {
    pub fn as_str(&self) -> &'static str {
        match self {
            ToolName::CalculateEmi => "calculate_emi",
            ToolName::AssessLoan => "assess_loan",
            ToolName::EmergencyFund => "emergency_fund",
            ToolName::SavingsProjection => "savings_projection",
            ToolName::CalculateTax => "calculate_tax",
            ToolName::InvestmentPlan => "generate_investment_plan",
            ToolName::CashflowPlan => "generate_cashflow",
            ToolName::GenerateBudget => "generate_budget",
            ToolName::ProfileSimilarity => "profile_similarity",
            ToolName::StatAnalysis => "stat_analysis",
        }
    }
}

impl FromStr for ToolName {
    type Err = AppError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "calculate_emi" => Ok(ToolName::CalculateEmi),
            "assess_loan" => Ok(ToolName::AssessLoan),
            "emergency_fund" => Ok(ToolName::EmergencyFund),
            "savings_projection" => Ok(ToolName::SavingsProjection),
            "calculate_tax" => Ok(ToolName::CalculateTax),
            "generate_investment_plan" => Ok(ToolName::InvestmentPlan),
            "generate_cashflow" => Ok(ToolName::CashflowPlan),
            "generate_budget" => Ok(ToolName::GenerateBudget),
            "profile_similarity" => Ok(ToolName::ProfileSimilarity),
            "stat_analysis" => Ok(ToolName::StatAnalysis),

            _ => Err(AppError::InvalidInput(format!("Unknown tool: {}", s))),
        }
    }
}

/// Returns tool definitions for LLM function calling
pub fn tool_definitions() -> Vec<Value> {
    vec![
        // ================= EMI =================
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

        // ================= Loan Assessment =================
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
                            "required": [
                                "monthly_income",
                                "requested_emi",
                                "credit_score",
                                "purpose"
                            ]
                        },
                        "policy": { "type": "object" }
                    },
                    "required": ["request", "policy"]
                }
            }
        }),

        // ================= Emergency Fund =================
        json!({
            "type": "function",
            "function": {
                "name": ToolName::EmergencyFund.as_str(),
                "description": "Calculate recommended emergency fund based on monthly expense and policy.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "monthly_expense": { "type": "number" },
                        "policy": { "type": "object" }
                    },
                    "required": ["monthly_expense", "policy"]
                }
            }
        }),

        // ================= Savings Projection =================
        json!({
            "type": "function",
            "function": {
                "name": ToolName::SavingsProjection.as_str(),
                "description": "Project savings growth over a number of months.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "months": { "type": "integer" },
                        "policy": { "type": "object" }
                    },
                    "required": ["months", "policy"]
                }
            }
        }),

        // ================= Tax Calculation =================
        json!({
            "type": "function",
            "function": {
                "name": ToolName::CalculateTax.as_str(),
                "description": "Calculate taxes based on amount and tax profile rules.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "amount": { "type": "number" },
                        "profile": { "type": "object" }
                    },
                    "required": ["amount", "profile"]
                }
            }
        }),
        // ================= Investment planner ==============
        json!({
            "type": "function",
            "function": {
                "name": ToolName::InvestmentPlan.as_str(),
                "description": "Generate an investment allocation plan based on investable amount and investor profile.",
                "parameters": {
                   "type": "object",
                    "properties": {
                        "investable_amount": { "type": "number" },
                        "profile": { "type": "object" }
                    },
                    "required": ["investable_amount", "profile"]
                }
            }
        }),
        // ================= Cashflow Tool ==================
        json!({
            "type": "function",
            "function": {
                "name": ToolName::CashflowPlan.as_str(),
                "description": "Generate a monthly cashflow allocation based on income and cashflow profile.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "monthly_income": { "type": "number" },
                        "profile": { "type": "object" }
                    },
                    "required": ["monthly_income", "profile"]
                }
            }
        }),
        // =============== generate budget =======================
        json!({
            "type": "function",
            "function": {
                "name": ToolName::GenerateBudget.as_str(),
                "description": "Generate a monthly budget allocation based on income and budget profile rules.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "monthly_income": { "type": "number" },
                        "profile": { "type": "object" }
                    },
                    "required": ["monthly_income", "profile"]
                }
            }
        }),
        // ============= Profile Similarity =======================
        json!({
            "type": "function",
            "function": {
                "name": ToolName::ProfileSimilarity.as_str(),
                "description": "Compute similarity score between two user profiles using a selected metric.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "a": { "type": "object" },
                        "b": { "type": "object" },
                        "metric": {
                            "type": "string",
                            "enum": ["Euclidean", "Cosine", "Pearson"]
                        }
                    },
                    "required": ["a", "b", "metric"]
                }
            }
        }),
        json!({
            "type": "function",
            "function": {
                "name": ToolName::StatAnalysis.as_str(),
                "description": "Analyze user stats to compute category scores and generate health/finance/productivity alerts.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "profile": { "type": "object" }
                    },
                    "required": ["profile"]
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
        ToolName::EmergencyFund => assistant::execute_emergency_fund_async(arguments).await,

        ToolName::SavingsProjection => assistant::execute_savings_projection_async(arguments).await,

        ToolName::CalculateTax => assistant::execute_calculate_tax_async(arguments).await,

        ToolName::InvestmentPlan => assistant::execute_investment_plan_async(arguments).await,

        ToolName::CashflowPlan => assistant::execute_cashflow_async(arguments).await,
        ToolName::GenerateBudget => assistant::execute_generate_budget(arguments).await,
        ToolName::ProfileSimilarity => assistant::execute_profile_similarity(arguments).await,
        ToolName::StatAnalysis => assistant::execute_stat_analysis_async(arguments).await,
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
