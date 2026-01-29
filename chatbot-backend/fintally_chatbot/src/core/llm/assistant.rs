use serde_json::{ json, Value };
use crate::core::finance::{
    emi::calculate_emi,
    loans::assess_loan_checked,
    savings::{ emergency_fund, savings_projection },
    tax::calculate_tax,
    investments::generate_investment_plan_checked,
    cashflow::generate_cashflow,
    budgeting::generate_budget,
};
use crate::core::math::{ similarity::similarity, stats::{ compute_stat_scores, generate_alerts } };
use crate::core::llm::dto::*;
use crate::core::utils::errors::AppError;
use crate::core::types::*;
use tokio::task;

/// Stat analysis tool (scores + alerts)
pub async fn execute_stat_analysis_async(args: Value) -> Result<Value, AppError> {
    let profile: StatProfile = serde_json
        ::from_value(
            args
                .get("profile")
                .cloned()
                .ok_or_else(|| AppError::InvalidInput("Missing stat profile".into()))?
        )
        .map_err(|e| AppError::InvalidInput(format!("Invalid stat profile JSON: {}", e)))?;

    let (scores, alerts) = task
        ::spawn_blocking(move || {
            let scores = compute_stat_scores(&profile)?;
            let alerts = generate_alerts(&profile)?;
            Ok::<_, AppError>((scores, alerts))
        }).await
        .map_err(|e| AppError::Other(format!("Task join error: {}", e)))??;

    Ok(json!({
        "scores": scores,
        "alerts": alerts
    }))
}

/// Profile similarity tool (async)
pub async fn execute_profile_similarity(args: Value) -> Result<Value, AppError> {
    let a: UserProfileVector = serde_json
        ::from_value(
            args
                .get("a")
                .cloned()
                .ok_or_else(|| AppError::InvalidInput("Missing profile A".into()))?
        )
        .map_err(|e| AppError::InvalidInput(format!("Invalid profile A JSON: {}", e)))?;

    let b: UserProfileVector = serde_json
        ::from_value(
            args
                .get("b")
                .cloned()
                .ok_or_else(|| AppError::InvalidInput("Missing profile B".into()))?
        )
        .map_err(|e| AppError::InvalidInput(format!("Invalid profile B JSON: {}", e)))?;

    let metric: SimilarityMetric = serde_json
        ::from_value(
            args
                .get("metric")
                .cloned()
                .ok_or_else(|| AppError::InvalidInput("Missing similarity metric".into()))?
        )
        .map_err(|e| AppError::InvalidInput(format!("Invalid similarity metric: {}", e)))?;

    let score = task
        ::spawn_blocking(move || { similarity(&a, &b, metric) }).await
        .map_err(|e| AppError::Other(format!("Task join error: {}", e)))??;

    Ok(json!({ "score": score }))
}

/// Generate budget tool (async)
pub async fn execute_generate_budget(args: Value) -> Result<Value, AppError> {
    let monthly_income = args
        .get("monthly_income")
        .and_then(Value::as_f64)
        .ok_or_else(|| AppError::InvalidInput("Missing monthly_income".into()))?;

    let profile: BudgetProfile = serde_json
        ::from_value(
            args
                .get("profile")
                .cloned()
                .ok_or_else(|| AppError::InvalidInput("Missing budget profile".into()))?
        )
        .map_err(|e| AppError::InvalidInput(format!("Invalid budget profile JSON: {}", e)))?;

    let budget = task
        ::spawn_blocking(move || { generate_budget(monthly_income, &profile) }).await
        .map_err(|e| AppError::Other(format!("Task join error: {}", e)))??;

    Ok(
        serde_json
            ::to_value(budget)
            .map_err(|e| AppError::Other(format!("Serialization error: {}", e)))?
    )
}

/// investment plan tool
pub async fn execute_investment_plan_async(args: Value) -> Result<Value, AppError> {
    let investable_amount = args
        .get("investable_amount")
        .and_then(Value::as_f64)
        .ok_or_else(|| AppError::InvalidInput("Missing investable_amount".into()))?;

    let profile: InvestmentProfile = serde_json
        ::from_value(
            args
                .get("profile")
                .cloned()
                .ok_or_else(|| { AppError::InvalidInput("Missing investment profile".into()) })?
        )
        .map_err(|e| AppError::InvalidInput(format!("Invalid investment profile JSON: {}", e)))?;

    let plan = tokio::task
        ::spawn_blocking(move || {
            generate_investment_plan_checked(investable_amount, &profile)
        }).await
        .map_err(|e| AppError::Other(e.to_string()))??;

    Ok(
        serde_json
            ::to_value(plan)
            .map_err(|e| AppError::Other(format!("Serialization error: {}", e)))?
    )
}

/// Cashflow tool
pub async fn execute_cashflow_async(args: Value) -> Result<Value, AppError> {
    let monthly_income = args
        .get("monthly_income")
        .and_then(Value::as_f64)
        .ok_or_else(|| AppError::InvalidInput("Missing monthly_income".into()))?;

    let profile: CashflowProfile = serde_json
        ::from_value(
            args
                .get("profile")
                .cloned()
                .ok_or_else(|| { AppError::InvalidInput("Missing cashflow profile".into()) })?
        )
        .map_err(|e| AppError::InvalidInput(format!("Invalid cashflow profile JSON: {}", e)))?;

    let result = tokio::task
        ::spawn_blocking(move || { generate_cashflow(monthly_income, &profile) }).await
        .map_err(|e| AppError::Other(e.to_string()))??;

    Ok(
        serde_json
            ::to_value(result)
            .map_err(|e| AppError::Other(format!("Serialization error: {}", e)))?
    )
}

/// Emergency fund tool
pub async fn execute_emergency_fund_async(args: Value) -> Result<Value, AppError> {
    let args: EmergencyFundArgs = serde_json
        ::from_value(args)
        .map_err(|e| AppError::InvalidInput(format!("Invalid emergency fund args: {}", e)))?;

    let fund = task
        ::spawn_blocking(move || { emergency_fund(args.monthly_expense, &args.policy) }).await
        .map_err(|e| AppError::Other(format!("Task join error: {}", e)))??;

    Ok(json!({ "emergency_fund": fund }))
}

/// Savings projection tool
pub async fn execute_savings_projection_async(args: Value) -> Result<Value, AppError> {
    let args: SavingsProjectionArgs = serde_json
        ::from_value(args)
        .map_err(|e| AppError::InvalidInput(format!("Invalid savings args: {}", e)))?;

    let total = task
        ::spawn_blocking(move || { savings_projection(args.months, &args.policy) }).await
        .map_err(|e| AppError::Other(format!("Task join error: {}", e)))??;

    Ok(json!({ "projected_savings": total }))
}

/// Tax calculation tool
pub async fn execute_calculate_tax_async(args: Value) -> Result<Value, AppError> {
    let args: TaxCalculationArgs = serde_json
        ::from_value(args)
        .map_err(|e| AppError::InvalidInput(format!("Invalid tax args: {}", e)))?;

    let taxes = task
        ::spawn_blocking(move || {
            calculate_tax(args.amount, &args.profile).map_err(AppError::from)
        }).await
        .map_err(|e| AppError::Other(format!("Task join error: {}", e)))??;

    Ok(json!({ "taxes": taxes }))
}

/// Execute EMI calculation tool asynchronously
pub async fn execute_calculate_emi_async(args: Value) -> Result<Value, AppError> {
    // Parse args strictly
    let args: CalculateEmiArgs = serde_json
        ::from_value(args)
        .map_err(|e| AppError::InvalidInput(format!("Invalid EMI arguments: {}", e)))?;

    // Run the CPU-bound EMI calculation in a blocking task
    let emi = task
        ::spawn_blocking(move || {
            calculate_emi(args.principal, args.annual_rate, args.tenure_months).map_err(|e|
                AppError::CalculationError(format!("EMI calculation failed: {}", e))
            )
        }).await
        .map_err(|e| AppError::Other(format!("Task join error: {}", e)))??;

    Ok(json!({ "emi": emi }))
}

/// Execute loan assessment tool asynchronously
pub async fn execute_assess_loan_async(args: Value) -> Result<Value, AppError> {
    let args: AssessLoanArgs = serde_json
        ::from_value(args)
        .map_err(|e| AppError::InvalidInput(format!("Invalid loan arguments: {}", e)))?;

    // Run CPU-bound loan assessment in blocking task
    let assessment = task
        ::spawn_blocking(move || { assess_loan_checked(&args.request, &args.policy) }).await
        .map_err(|e| AppError::Other(format!("Task join error: {}", e)))??;

    Ok(
        json!({
        "approved": assessment.approved,
        "max_allowed_emi": assessment.max_allowed_emi,
        "risk_score": assessment.risk_score,
        "reason": assessment.reason
    })
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    // ---------------- EMI ----------------

    #[tokio::test]
    async fn calculate_emi_success() {
        let args =
            json!({
            "principal": 100_000,
            "annual_rate": 12.0,
            "tenure_months": 12
        });

        let result = execute_calculate_emi_async(args).await.unwrap();
        assert!(result.get("emi").is_some());
    }

    // ---------------- Loan Assessment ----------------

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

    // ---------------- Emergency Fund ----------------

    #[tokio::test]
    async fn emergency_fund_success() {
        let args =
            json!({
            "monthly_expense": 30_000.0,
            "policy": EmergencyFundPolicy::default()
        });

        let result = execute_emergency_fund_async(args).await.unwrap();
        assert!(result.get("emergency_fund").is_some());
    }

    // ---------------- Savings Projection ----------------

    #[tokio::test]
    async fn savings_projection_success() {
        let args =
            json!({
            "months": 12,
            "policy": SavingsPolicy::default()
        });

        let result = execute_savings_projection_async(args).await.unwrap();
        assert!(result.get("projected_savings").is_some());
    }

    // ---------------- Budget ----------------

    #[tokio::test]
    async fn generate_budget_young_professional() {
        let args =
            json!({
            "monthly_income": 80_000.0,
            "profile": BudgetProfile::single_young_professional()
        });

        let result = execute_generate_budget(args).await.unwrap();
        assert!(result.is_object());
    }

    // ---------------- Investment Plan ----------------
    // ❌ InvestmentProfile::moderate() does NOT exist
    // ✅ Use config presets instead

    #[tokio::test]
    async fn investment_plan_rejects_over_allocation_for_growing_family() {
        let args =
            json!({
        "investable_amount": 50_000.0,
        "profile": InvestmentProfile::growing_family_balanced()
    });

        let result = execute_investment_plan_async(args).await;

        match result {
            Err(AppError::Domain(DomainError::AllocationOverflow { attempted, available })) => {
                assert!(attempted > available);
            }
            _ => panic!("Expected AllocationOverflow for growing family profile"),
        }
    }

    // ---------------- Cashflow ----------------
    // ❌ CashflowProfile::standard() does NOT exist
    // ✅ Use real presets

    #[tokio::test]
    async fn cashflow_generation_success() {
        let args =
            json!({
            "monthly_income": 80_000.0,
            "profile": CashflowProfile::young_professional()
        });

        let result = execute_cashflow_async(args).await.unwrap();
        assert!(result.is_object());
    }

    // ---------------- Profile Similarity ----------------
    // ❌ sample_a / sample_b do NOT exist → define real vectors

    #[tokio::test]
    async fn profile_similarity_success() {
        let a = UserProfileVector {
            user_id: "user_a".to_string(),
            metrics: vec![80_000.0, 28.0, 0.6, 0.0, 0.25],
        };

        let b = UserProfileVector {
            user_id: "user_b".to_string(),
            metrics: vec![75_000.0, 30.0, 0.55, 1.0, 0.2],
        };

        let args = json!({
        "a": a,
        "b": b,
        "metric": "Cosine"
    });

        let result = execute_profile_similarity(args).await.unwrap();
        assert!(result.get("score").is_some());
    }
}
