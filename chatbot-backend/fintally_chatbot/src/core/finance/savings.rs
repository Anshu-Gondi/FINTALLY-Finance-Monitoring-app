// src/core/finance/savings.rs

use crate::core::types::{EmergencyFundPolicy, SavingsPolicy};
use crate::core::utils::errors::AppError;

/// Calculate emergency fund with validation
pub fn emergency_fund(
    monthly_expense: f64,
    policy: &EmergencyFundPolicy,
) -> Result<f64, AppError> {
    if monthly_expense < 0.0 {
        return Err(AppError::InvalidInput(
            "Monthly expense cannot be negative".to_string(),
        ));
    }
    if policy.months == 0.0 || policy.expense_multiplier <= 0.0 {
        return Err(AppError::InvalidInput(
            "Invalid emergency fund policy parameters".to_string(),
        ));
    }

    Ok(monthly_expense * policy.months as f64 * policy.expense_multiplier)
}

/// Project savings over a number of months with validation
pub fn savings_projection(
    months: u32,
    policy: &SavingsPolicy,
) -> Result<f64, AppError> {
    if months == 0 {
        return Err(AppError::InvalidInput(
            "Number of months must be positive".to_string(),
        ));
    }
    if policy.monthly_contribution < 0.0 || policy.annual_growth_rate < 0.0 {
        return Err(AppError::InvalidInput(
            "Savings policy cannot have negative contributions or growth rate".to_string(),
        ));
    }

    let monthly_rate = policy.annual_growth_rate / 12.0;
    let mut total = 0.0;

    for _ in 0..months {
        total = total * (1.0 + monthly_rate) + policy.monthly_contribution;
    }

    Ok(total)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::types::FinanceProfile;

    #[test]
    fn emergency_fund_default_profile() {
        let profile = FinanceProfile::default();
        let fund = emergency_fund(20_000.0, &profile.emergency_fund).unwrap();
        assert_eq!(fund, 120_000.0);
    }

    #[test]
    fn emergency_fund_millionaire_profile() {
        let profile = FinanceProfile::millionaire();
        let fund = emergency_fund(100_000.0, &profile.emergency_fund).unwrap();
        assert_eq!(fund, 100_000.0);
    }

    #[test]
    fn emergency_fund_negative_expense() {
        let profile = FinanceProfile::default();
        let result = emergency_fund(-10_000.0, &profile.emergency_fund);
        assert!(matches!(result, Err(AppError::InvalidInput(_))));
    }

    #[test]
    fn savings_projection_zero_months() {
        let policy = FinanceProfile::default().savings;
        let result = savings_projection(0, &policy);
        assert!(matches!(result, Err(AppError::InvalidInput(_))));
    }
}
