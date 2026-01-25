// src/core/llm/dto.rs
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize)]
pub struct CalculateEmiArgs {
    pub principal: f64,
    pub annual_rate: f64,
    pub tenure_months: u32,
}

#[derive(Debug, Deserialize)]
pub struct AssessLoanArgs {
    pub request: crate::core::types::LoanRequest,
    pub policy: crate::core::types::LoanPolicy,
}

#[derive(Debug, Serialize)]
pub struct EmiResult {
    pub emi: f64,
}

#[derive(Debug, Serialize)]
pub struct LoanAssessmentResult {
    pub approved: bool,
    pub max_allowed_emi: f64,
    pub risk_score: f64,
    pub reason: String,
}
