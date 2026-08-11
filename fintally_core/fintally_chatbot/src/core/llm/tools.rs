// src/core/llm/tools.rs

use crate::core::llm::assistant;
use crate::core::types::*;
use crate::core::utils::errors::AppError;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::fmt;
use std::str::FromStr;

// ============================================================================
// 1. Diagnostic Data Model
// ============================================================================

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum DiagnosticStatus {
    UnknownTool,
    MissingArguments,
    TypeMismatch,
    DomainConstraintViolation,
    ExecutionFailed,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FieldError {
    pub field: String,
    pub expected_type: String,
    pub actual_value: Option<Value>,
    pub issue: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ToolDiagnosticReport {
    pub tool_name: String,
    pub status: DiagnosticStatus,
    pub error_summary: String,
    pub missing_fields: Vec<String>,
    pub invalid_fields: Vec<FieldError>,
    pub expected_schema: Option<Value>,
    pub available_tools: Vec<String>,
    pub remediation_prompt: String,
}

impl ToolDiagnosticReport {
    /// Formats the diagnostic report into an LLM-friendly correction prompt
    pub fn to_llm_payload(&self) -> Value {
        json!({
            "status": "error",
            "diagnostic_report": self
        })
    }
}

impl fmt::Display for ToolDiagnosticReport {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if !self.remediation_prompt.is_empty() {
            write!(
                f,
                "[{:?}] Tool '{}' failed: {}. Remediation: {}",
                self.status, self.tool_name, self.error_summary, self.remediation_prompt
            )
        } else {
            write!(
                f,
                "[{:?}] Tool '{}' failed: {}",
                self.status, self.tool_name, self.error_summary
            )
        }
    }
}

// ============================================================================
// 2. Tool Definition & Enum Registry
// ============================================================================

#[derive(Debug, Clone, PartialEq, Eq)]
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

    pub fn all_names() -> Vec<String> {
        vec![
            ToolName::CalculateEmi.as_str().to_string(),
            ToolName::AssessLoan.as_str().to_string(),
            ToolName::EmergencyFund.as_str().to_string(),
            ToolName::SavingsProjection.as_str().to_string(),
            ToolName::CalculateTax.as_str().to_string(),
            ToolName::InvestmentPlan.as_str().to_string(),
            ToolName::CashflowPlan.as_str().to_string(),
            ToolName::GenerateBudget.as_str().to_string(),
            ToolName::ProfileSimilarity.as_str().to_string(),
            ToolName::StatAnalysis.as_str().to_string(),
        ]
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

/// Retrieve full JSON schema definition for a specific tool
pub fn get_tool_schema(tool_name: &str) -> Option<Value> {
    tool_definitions().into_iter().find(|def| {
        def.get("function")
            .and_then(|f| f.get("name"))
            .and_then(|n| n.as_str())
            == Some(tool_name)
    })
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
                        "principal": { "type": "number", "description": "Total loan principal amount" },
                        "annual_rate": { "type": "number", "description": "Annual interest rate percentage (e.g. 8.5)" },
                        "tenure_months": { "type": "integer", "description": "Tenure in months" }
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
                            "description": "Select a loan policy variant"
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
                            "description": "Loan policy variant affecting emergency fund calculations"
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
                            "description": "Optional loan policy variant"
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

// ============================================================================
// 3. Pre-Execution Schema Validator
// ============================================================================

pub fn validate_arguments(tool_name: &str, arguments: &mut Value) -> Result<(), ToolDiagnosticReport> {
    let schema = match get_tool_schema(tool_name) {
        Some(s) => s,
        None => {
            return Err(ToolDiagnosticReport {
                tool_name: tool_name.to_string(),
                status: DiagnosticStatus::UnknownTool,
                error_summary: format!("Tool '{}' does not exist in registry.", tool_name),
                missing_fields: vec![],
                invalid_fields: vec![],
                expected_schema: None,
                available_tools: ToolName::all_names(),
                remediation_prompt: format!(
                    "Tool '{}' was not found. Please select from available tools: {}",
                    tool_name,
                    ToolName::all_names().join(", ")
                ),
            });
        }
    };

    let params = &schema["function"]["parameters"];
    let empty_vec = vec![];
    let required_fields = params["required"].as_array().unwrap_or(&empty_vec);
    let properties = &params["properties"];

    let mut missing_fields = Vec::new();
    let mut invalid_fields = Vec::new();

    // Check required fields
    for req in required_fields {
        if let Some(req_str) = req.as_str() {
            if arguments.get(req_str).is_none() || arguments[req_str].is_null() {
                missing_fields.push(req_str.to_string());
            }
        }
    }

    // Check type constraints and auto-coerce numeric strings if valid
    if let Some(args_obj) = arguments.as_object_mut() {
        for (key, val) in args_obj.iter_mut() {
            if let Some(prop_schema) = properties.get(key) {
                if let Some(expected_type) = prop_schema.get("type").and_then(|t| t.as_str()) {
                    let is_valid = match expected_type {
                        "number" => {
                            if val.is_number() {
                                true
                            } else if let Some(s) = val.as_str() {
                                let cleaned = s.replace('₹', "").replace('$', "").replace(',', "").replace(' ', "");
                                if let Ok(parsed) = cleaned.parse::<f64>() {
                                    *val = json!(parsed);
                                    true
                                } else {
                                    false
                                }
                            } else {
                                false
                            }
                        }
                        "integer" => {
                            if val.is_i64() || val.is_u64() {
                                true
                            } else if let Some(s) = val.as_str() {
                                let cleaned = s.replace('₹', "").replace('$', "").replace(',', "").replace(' ', "");
                                if let Ok(parsed) = cleaned.parse::<i64>() {
                                    *val = json!(parsed);
                                    true
                                } else {
                                    false
                                }
                            } else {
                                false
                            }
                        }
                        "string" => val.is_string(),
                        "boolean" => val.is_boolean(),
                        "object" => val.is_object(),
                        "array" => val.is_array(),
                        _ => true,
                    };

                    if !is_valid {
                        invalid_fields.push(FieldError {
                            field: key.clone(),
                            expected_type: expected_type.to_string(),
                            actual_value: Some(val.clone()),
                            issue: format!("Value for field '{}' must be of type '{}'.", key, expected_type),
                        });
                    }
                }

                // Enum validation for string params
                if let Some(enum_vals) = prop_schema.get("enum").and_then(|e| e.as_array()) {
                    if let Some(val_str) = val.as_str() {
                        let matches_enum = enum_vals.iter().any(|e| e.as_str() == Some(val_str));
                        if !matches_enum {
                            let allowed: Vec<String> = enum_vals
                                .iter()
                                .filter_map(|e| e.as_str().map(|s| s.to_string()))
                                .collect();

                            invalid_fields.push(FieldError {
                                field: key.clone(),
                                expected_type: "enum_string".to_string(),
                                actual_value: Some(val.clone()),
                                issue: format!(
                                    "Invalid enum value '{}'. Allowed values are: [{}]",
                                    val_str,
                                    allowed.join(", ")
                                ),
                            });
                        }
                    }
                }
            }
        }
    }

    if !missing_fields.is_empty() || !invalid_fields.is_empty() {
        let status = if !missing_fields.is_empty() {
            DiagnosticStatus::MissingArguments
        } else {
            DiagnosticStatus::TypeMismatch
        };

        let remediation = if !missing_fields.is_empty() {
            format!(
                "Tool '{}' requires missing parameters: [{}]. Ask the user for these details or supply them.",
                tool_name,
                missing_fields.join(", ")
            )
        } else {
            format!(
                "Tool '{}' received invalid arguments for fields: [{}]. Correct the parameter types or enum values.",
                tool_name,
                invalid_fields.iter().map(|f| f.field.as_str()).collect::<Vec<_>>().join(", ")
            )
        };

        return Err(ToolDiagnosticReport {
            tool_name: tool_name.to_string(),
            status,
            error_summary: "Validation failed before execution.".to_string(),
            missing_fields,
            invalid_fields,
            expected_schema: Some(params.clone()),
            available_tools: ToolName::all_names(),
            remediation_prompt: remediation,
        });
    }

    Ok(())
}

// ============================================================================
// 4. Async Execution Engine with Diagnostic Error Intercept
// ============================================================================

/// Executes a tool asynchronously, producing rich diagnostics on failure
pub async fn execute_tool_async(
    tool_name: &str,
    mut arguments: Value,
) -> Result<Value, ToolDiagnosticReport> {
    // Phase 1: Pre-execution validation (mutably coerces formatted strings into numbers)
    validate_arguments(tool_name, &mut arguments)?;

    let tool = ToolName::from_str(tool_name).map_err(|_| ToolDiagnosticReport {
        tool_name: tool_name.to_string(),
        status: DiagnosticStatus::UnknownTool,
        error_summary: format!("Unknown tool '{}'", tool_name),
        missing_fields: vec![],
        invalid_fields: vec![],
        expected_schema: None,
        available_tools: ToolName::all_names(),
        remediation_prompt: format!("Unknown tool: {}", tool_name),
    })?;

    // Phase 2: Domain Entity Resolution & Fallbacks
    let loan_policy: Option<LoanPolicy> = if let Some(policy_val) = arguments.get("policy") {
        if let Some(policy_str) = policy_val.as_str() {
            match LoanPolicy::from_name(policy_str) {
                Ok(p) => Some(p),
                Err(e) => {
                    return Err(ToolDiagnosticReport {
                        tool_name: tool_name.to_string(),
                        status: DiagnosticStatus::TypeMismatch,
                        error_summary: format!("Failed to parse LoanPolicy: {}", e),
                        missing_fields: vec![],
                        invalid_fields: vec![FieldError {
                            field: "policy".to_string(),
                            expected_type: "LoanPolicy enum".to_string(),
                            actual_value: Some(policy_val.clone()),
                            issue: format!("Allowed policy values: {:?}", LoanPolicy::variants()),
                        }],
                        expected_schema: get_tool_schema(tool_name),
                        available_tools: ToolName::all_names(),
                        remediation_prompt: format!(
                            "Invalid policy string '{}'. Must be one of: {:?}",
                            policy_str,
                            LoanPolicy::variants()
                        ),
                    });
                }
            }
        } else {
            None
        }
    } else {
        // Fallback default policy if none is specified
        Some(LoanPolicy::salaried())
    };

    if let Some(ref policy) = loan_policy {
        arguments["policy"] = serde_json::to_value(policy).map_err(|e| ToolDiagnosticReport {
            tool_name: tool_name.to_string(),
            status: DiagnosticStatus::ExecutionFailed,
            error_summary: format!("Serialization failure: {}", e),
            missing_fields: vec![],
            invalid_fields: vec![],
            expected_schema: None,
            available_tools: ToolName::all_names(),
            remediation_prompt: "Internal serialization failure.".into(),
        })?;
    }

    let tax_profile: Option<TaxProfile> = if let Some(tax_val) = arguments.get("tax_profile") {
        if let Some(tax_str) = tax_val.as_str() {
            match TaxProfile::from_name(tax_str, None) {
                Ok(t) => Some(t),
                Err(e) => {
                    return Err(ToolDiagnosticReport {
                        tool_name: tool_name.to_string(),
                        status: DiagnosticStatus::TypeMismatch,
                        error_summary: format!("Failed to resolve TaxProfile: {}", e),
                        missing_fields: vec![],
                        invalid_fields: vec![FieldError {
                            field: "tax_profile".to_string(),
                            expected_type: "TaxProfile string".to_string(),
                            actual_value: Some(tax_val.clone()),
                            issue: "Tax profile name unrecognized.".to_string(),
                        }],
                        expected_schema: get_tool_schema(tool_name),
                        available_tools: ToolName::all_names(),
                        remediation_prompt: format!("Provide a valid tax profile name. Details: {}", e),
                    });
                }
            }
        } else {
            None
        }
    } else {
        None
    };

    if let Some(ref tax) = tax_profile {
        arguments["tax_profile"] = serde_json::to_value(tax).map_err(|e| ToolDiagnosticReport {
            tool_name: tool_name.to_string(),
            status: DiagnosticStatus::ExecutionFailed,
            error_summary: format!("Serialization failure: {}", e),
            missing_fields: vec![],
            invalid_fields: vec![],
            expected_schema: None,
            available_tools: ToolName::all_names(),
            remediation_prompt: "Internal serialization failure.".into(),
        })?;
    }

    if let Some(profile_str) = arguments.get("profile").and_then(|v| v.as_str()) {
        match StatProfile::from_name(profile_str, tax_profile.as_ref(), loan_policy.as_ref()) {
            Ok(stat_profile) => {
                arguments["profile"] =
                    serde_json::to_value(&stat_profile).map_err(|e| ToolDiagnosticReport {
                        tool_name: tool_name.to_string(),
                        status: DiagnosticStatus::ExecutionFailed,
                        error_summary: format!("Failed to serialize StatProfile: {}", e),
                        missing_fields: vec![],
                        invalid_fields: vec![],
                        expected_schema: None,
                        available_tools: ToolName::all_names(),
                        remediation_prompt: "Internal state error.".into(),
                    })?;
            }
            Err(e) => {
                return Err(ToolDiagnosticReport {
                    tool_name: tool_name.to_string(),
                    status: DiagnosticStatus::DomainConstraintViolation,
                    error_summary: format!("StatProfile constraint failed: {}", e),
                    missing_fields: vec![],
                    invalid_fields: vec![FieldError {
                        field: "profile".to_string(),
                        expected_type: "StatProfile variant".to_string(),
                        actual_value: Some(json!(profile_str)),
                        issue: format!("Profile domain error: {}", e),
                    }],
                    expected_schema: get_tool_schema(tool_name),
                    available_tools: ToolName::all_names(),
                    remediation_prompt: format!(
                        "Profile parameter '{}' violates domain rules: {}. Choose a valid StatProfile.",
                        profile_str, e
                    ),
                });
            }
        }
    }

    // Phase 3: Function Dispatch
    let result = match tool {
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
    };

    result.map_err(|err| ToolDiagnosticReport {
        tool_name: tool_name.to_string(),
        status: DiagnosticStatus::ExecutionFailed,
        error_summary: format!("Tool execution runtime error: {}", err),
        missing_fields: vec![],
        invalid_fields: vec![],
        expected_schema: get_tool_schema(tool_name),
        available_tools: ToolName::all_names(),
        remediation_prompt: format!(
            "Execution of tool '{}' failed: {}. Review arguments or ask user for clarified input.",
            tool_name, err
        ),
    })
}
