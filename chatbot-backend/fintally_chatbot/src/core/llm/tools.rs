// src/core/llm/tools.rs

use crate::core::llm::assistant;
use crate::core::utils::errors::AppError;
use crate::core::types::*;
use serde_json::{ json, Value };
use std::str::FromStr;

/// Convert internal AppError into LLM-safe, user-facing error payload
fn llm_safe_error(err: AppError) -> AppError {
    use crate::core::utils::domain_error::DomainError;

    match err {
        AppError::InvalidInput(_) => {
            AppError::InvalidInput(
                "Invalid or missing input parameters. Please correct the arguments and retry.".into()
            )
        }

        AppError::Domain(DomainError::AllocationOverflow { .. }) => {
            AppError::InvalidInput(
                "Investment amount exceeds allowed allocation for the selected profile.".into()
            )
        }

        AppError::Domain(_) => {
            AppError::InvalidInput(
                "The request violates domain rules for this financial calculation.".into()
            )
        }

        _ => AppError::Other("Internal calculation error. Please try again later.".into()),
    }
}

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
                                "purpose": { "type": "string", "enum": ["Personal", "Home", "Education", "Auto"] },
                                "is_joint": { "type": "boolean" }
                            },
                            "required": ["monthly_income", "requested_emi", "credit_score", "purpose"]
                        },
                        "policy": {
                            "type": "string",
                            "enum": LoanPolicy::variants(),
                            "description": "Select a loan policy variant (salaried, self_employed, etc.)"
                        }
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
                        "policy": {
                            "type": "string",
                            "enum": LoanPolicy::variants(),
                            "description": "Loan policy variant that may affect emergency fund calculations"
                        }
                    },
                    "required": ["monthly_expense"]
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
                        "policy": { "type": "string", "enum": LoanPolicy::variants() }
                    },
                    "required": ["months"]
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
                        "profile": { "type": "string", "description": "Tax profile name or variant" }
                    },
                    "required": ["amount", "profile"]
                }
            }
        }),

        // ================= Investment Planner =================
        json!({
            "type": "function",
            "function": {
                "name": ToolName::InvestmentPlan.as_str(),
                "description": "Generate an investment allocation plan based on investable amount and investor profile.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "investable_amount": { "type": "number" },
                        "profile": {
                            "type": "string",
                            "enum": [
                                "young_professional",
                                "family_with_dependents",
                                "retiree_income_focused",
                                "single_parent"
                            ],
                            "description": "StatProfile variant name"
                        },
                        "loan_policy": {
                            "type": "string",
                            "enum": LoanPolicy::variants(),
                            "description": "Optional loan policy variant affecting the investment plan"
                        }
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
                        "profile": {
                            "type": "string",
                            "enum": [
                                "young_professional",
                                "family_with_dependents",
                                "retiree_income_focused",
                                "single_parent"
                            ],
                            "description": "StatProfile variant"
                        },
                        "loan_policy": { "type": "string", "enum": LoanPolicy::variants() }
                    },
                    "required": ["monthly_income", "profile"]
                }
            }
        }),

        // ================= Budget =================
        json!({
            "type": "function",
            "function": {
                "name": ToolName::GenerateBudget.as_str(),
                "description": "Generate a monthly budget allocation based on income and budget profile rules.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "monthly_income": { "type": "number" },
                        "profile": {
                            "type": "string",
                            "enum": [
                                "young_professional",
                                "family_with_dependents",
                                "retiree_income_focused",
                                "single_parent"
                            ]
                        },
                        "loan_policy": { "type": "string", "enum": LoanPolicy::variants() }
                    },
                    "required": ["monthly_income", "profile"]
                }
            }
        }),

        // ================= Profile Similarity =================
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

        // ================= Stat Analysis =================
        json!({
            "type": "function",
            "function": {
                "name": ToolName::StatAnalysis.as_str(),
                "description": "Analyze user stats to compute category scores and generate health/finance/productivity alerts.",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "profile": {
                            "type": "string",
                            "enum": [
                                "young_professional",
                                "family_with_dependents",
                                "retiree_income_focused",
                                "single_parent"
                            ],
                            "description": "StatProfile variant name"
                        },
                        "loan_policy": { "type": "string", "enum": LoanPolicy::variants() },
                        "tax_profile": { "type": "string", "description": "Optional tax profile name" }
                    },
                    "required": ["profile"]
                }
            }
        })
    ]
}

/// Executes a tool based on name and arguments (async version)
pub async fn execute_tool_async(
    tool_name: &str,
    mut arguments: Value
) -> Result<Value, AppError> {
    let tool = ToolName::from_str(tool_name)?;

    // ===== Resolve LoanPolicy FIRST (owned) =====
    let loan_policy: Option<LoanPolicy> =
        arguments
            .get("policy")
            .and_then(|v| v.as_str())
            .map(|policy_str| LoanPolicy::from_name(policy_str))
            .transpose()?;

    if let Some(ref policy) = loan_policy {
        arguments["policy"] = serde_json::to_value(policy)
            .map_err(|e| AppError::Other(format!(
                "Failed to serialize LoanPolicy: {}", e
            )))?;
    }

    // ===== Resolve TaxProfile FIRST (owned) =====
    let tax_profile: Option<TaxProfile> =
        arguments
            .get("tax_profile")
            .and_then(|v| v.as_str())
            .map(|tax_str| TaxProfile::from_name(tax_str, None))
            .transpose()?;

    if let Some(ref tax) = tax_profile {
        arguments["tax_profile"] = serde_json::to_value(tax)
            .map_err(|e| AppError::Other(format!(
                "Failed to serialize TaxProfile: {}", e
            )))?;
    }

    // ===== Resolve StatProfile LAST (needs refs) =====
    if let Some(profile_str) = arguments.get("profile").and_then(|v| v.as_str()) {
        let stat_profile = StatProfile::from_name(
            profile_str,
            tax_profile.as_ref(),
            loan_policy.as_ref()
        )?;

        arguments["profile"] = serde_json::to_value(&stat_profile)
            .map_err(|e| AppError::Other(format!(
                "Failed to serialize StatProfile: {}", e
            )))?;
    }

    // ===== Dispatch =====
    let result = match tool {
        ToolName::CalculateEmi =>
            assistant::execute_calculate_emi_async(arguments).await,

        ToolName::AssessLoan =>
            assistant::execute_assess_loan_async(arguments).await,

        ToolName::EmergencyFund =>
            assistant::execute_emergency_fund_async(arguments).await,

        ToolName::SavingsProjection =>
            assistant::execute_savings_projection_async(arguments).await,

        ToolName::CalculateTax =>
            assistant::execute_calculate_tax_async(arguments).await,

        ToolName::InvestmentPlan =>
            assistant::execute_investment_plan_async(arguments).await,

        ToolName::CashflowPlan =>
            assistant::execute_cashflow_async(arguments).await,

        ToolName::GenerateBudget =>
            assistant::execute_generate_budget(arguments).await,

        ToolName::ProfileSimilarity =>
            assistant::execute_profile_similarity(arguments).await,

        ToolName::StatAnalysis =>
            assistant::execute_stat_analysis_async(arguments).await,
    };

    result.map_err(llm_safe_error)
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

    #[tokio::test]
    async fn toolname_as_str_roundtrip() {
        let tools = vec![
            ToolName::CalculateEmi,
            ToolName::AssessLoan,
            ToolName::EmergencyFund,
            ToolName::SavingsProjection,
            ToolName::CalculateTax,
            ToolName::InvestmentPlan,
            ToolName::CashflowPlan,
            ToolName::GenerateBudget,
            ToolName::ProfileSimilarity,
            ToolName::StatAnalysis
        ];

        for tool in tools {
            let name = tool.as_str();
            let parsed = ToolName::from_str(name).expect("ToolName should parse from as_str()");
            assert_eq!(parsed.as_str(), name);
        }
    }

    #[test]
    fn from_str_unknown_tool_errors() {
        let result = ToolName::from_str("definitely_not_real");
        assert!(result.is_err());

        let err = result.unwrap_err().to_string();
        assert!(err.contains("Unknown tool"));
    }

    #[test]
    fn tool_definitions_have_unique_names() {
        let defs = tool_definitions();

        let mut names: std::collections::HashSet<String> = std::collections::HashSet::new();

        for def in defs {
            let name = def["function"]["name"]
                .as_str()
                .expect("tool definition must have function.name")
                .to_string(); // 👈 OWN it

            assert!(names.insert(name), "Duplicate tool name found in tool_definitions");
        }
    }

    #[test]
    fn all_enum_tools_exist_in_definitions() {
        let defs = tool_definitions();

        let def_names: std::collections::HashSet<String> = defs
            .iter()
            .map(|d| {
                d["function"]["name"].as_str().unwrap().to_string() // 👈 OWN it
            })
            .collect();

        let enum_tools = vec![
            ToolName::CalculateEmi,
            ToolName::AssessLoan,
            ToolName::EmergencyFund,
            ToolName::SavingsProjection,
            ToolName::CalculateTax,
            ToolName::InvestmentPlan,
            ToolName::CashflowPlan,
            ToolName::GenerateBudget,
            ToolName::ProfileSimilarity,
            ToolName::StatAnalysis
        ];

        for tool in enum_tools {
            let name = tool.as_str().to_string();
            assert!(
                def_names.contains(&name),
                "Tool '{}' exists in enum but not in tool_definitions()",
                name
            );
        }
    }

    #[tokio::test]
    async fn stat_analysis_requires_profile_argument() {
        let args = json!({}); // missing profile
        let result = execute_tool_async(ToolName::StatAnalysis.as_str(), args).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn investment_plan_rejects_invalid_profile_shape() {
        let args =
            json!({
        "investable_amount": 50000,
        "profile": { "risk": "high" }
    });

        let result = execute_tool_async("generate_investment_plan", args).await;
        assert!(result.is_err());
    }
}
