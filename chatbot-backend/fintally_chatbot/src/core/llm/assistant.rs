use serde_json::{json, Value};
use crate::core::finance::{emi::calculate_emi, loans::assess_loan_checked};
use crate::core::llm::dto::*;
use crate::core::utils::errors::AppError;
use tokio::task;

/// Execute EMI calculation tool asynchronously
pub async fn execute_calculate_emi_async(args: Value) -> Result<Value, AppError> {
    // Parse args strictly
    let args: CalculateEmiArgs = serde_json::from_value(args)
        .map_err(|e| AppError::InvalidInput(format!("Invalid EMI arguments: {}", e)))?;

    // Run the CPU-bound EMI calculation in a blocking task
    let emi = task::spawn_blocking(move || {
        calculate_emi(args.principal, args.annual_rate, args.tenure_months)
            .map_err(|e| AppError::CalculationError(format!("EMI calculation failed: {}", e)))
    })
    .await
    .map_err(|e| AppError::Other(format!("Task join error: {}", e)))??;

    Ok(json!({ "emi": emi }))
}

/// Execute loan assessment tool asynchronously
pub async fn execute_assess_loan_async(args: Value) -> Result<Value, AppError> {
    let args: AssessLoanArgs = serde_json::from_value(args)
        .map_err(|e| AppError::InvalidInput(format!("Invalid loan arguments: {}", e)))?;

    // Run CPU-bound loan assessment in blocking task
    let assessment = task::spawn_blocking(move || {
        assess_loan_checked(&args.request, &args.policy)
    })
    .await
    .map_err(|e| AppError::Other(format!("Task join error: {}", e)))??;

    Ok(json!({
        "approved": assessment.approved,
        "max_allowed_emi": assessment.max_allowed_emi,
        "risk_score": assessment.risk_score,
        "reason": assessment.reason
    }))
}


#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use crate::core::types::*;

    // Use tokio::test for async tests
    #[tokio::test]
    async fn calculate_emi_success() {
        let args = json!({
            "principal": 100_000,
            "annual_rate": 12.0,
            "tenure_months": 12
        });

        let result = execute_calculate_emi_async(args).await.unwrap();
        assert!(result.get("emi").is_some());
    }

    #[tokio::test]
    async fn assess_loan_success() {
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

        let result = execute_assess_loan_async(args).await.unwrap();
        assert!(result.get("approved").is_some());
        assert!(result.get("reason").unwrap().as_str().unwrap().len() > 0);
    }
}
