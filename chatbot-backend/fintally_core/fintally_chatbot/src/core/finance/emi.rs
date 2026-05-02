// src/core/finance/emi.rs

use crate::core::types::*;
use crate::core::utils::domain_error::EmiError;

pub fn calculate_emi(
    principal: f64,
    annual_rate: f64,
    tenure_months: u32,
) -> Result<f64, EmiError> {
    if principal <= 0.0 {
        return Err(EmiError::InvalidPrincipal(principal));
    }
    if annual_rate <= 0.0 {
        return Err(EmiError::InvalidRate(annual_rate));
    }
    if tenure_months == 0 {
        return Err(EmiError::InvalidTenure(tenure_months));
    }

    let r = annual_rate / 12.0 / 100.0;
    if r.abs() < f64::EPSILON {
        return Err(EmiError::InvalidRate(annual_rate));
    }
    let n = tenure_months as f64;

    let factor = (1.0 + r).powf(n);
    let denom = factor - 1.0;

    if denom.abs() < f64::EPSILON {
        return Err(EmiError::InvalidRate(annual_rate));
    }

    let emi = principal * r * factor / denom;

    if !emi.is_finite() || emi <= 0.0 {
        return Err(EmiError::InvalidEmi(emi));
    }

    Ok(emi)
}

pub fn is_emi_affordable(
    emi: f64,
    monthly_income: f64,
    policy: &EmiPolicy,
) -> Result<(), EmiError> {
    if !emi.is_finite() || emi <= 0.0 {
        return Err(EmiError::InvalidEmi(emi));
    }

    if monthly_income <= 0.0 {
        return Err(EmiError::IncomeTooLow(monthly_income));
    }

    // Policy invariants
    if policy.max_emi_percent <= 0.0
        || policy.max_emi_percent > 100.0
        || policy.min_surplus_percent < 0.0
        || policy.min_surplus_percent > 100.0
        || policy.max_emi_percent + policy.min_surplus_percent > 100.0
    {
        return Err(EmiError::InvalidPolicy);
    }

    let emi_percent = (emi / monthly_income) * 100.0;

    if emi_percent > policy.max_emi_percent {
        return Err(EmiError::EmiTooHigh {
            emi_percent,
            max_allowed: policy.max_emi_percent,
        });
    }

    let surplus_percent = 100.0 - emi_percent;

    if surplus_percent < policy.min_surplus_percent {
        return Err(EmiError::InsufficientSurplus {
            surplus_percent,
            required: policy.min_surplus_percent,
        });
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::types::EmiPolicy;

    #[test]
    fn salaried_emi_within_limit() {
        let policy = EmiPolicy::salaried();
        let emi = 20_000.0;
        let income = 60_000.0;

        assert!(is_emi_affordable(emi, income, &policy).is_ok());
    }

    #[test]
    fn salaried_emi_exceeds_limit() {
        let policy = EmiPolicy::salaried();
        let emi = 30_000.0;
        let income = 60_000.0;

        let err = is_emi_affordable(emi, income, &policy).unwrap_err();
        assert!(matches!(err, EmiError::EmiTooHigh { .. }));
    }

    #[test]
    fn self_employed_stricter_cap() {
        let policy = EmiPolicy::self_employed();
        let emi = 20_000.0;
        let income = 50_000.0;

        let err = is_emi_affordable(emi, income, &policy).unwrap_err();
        assert!(matches!(err, EmiError::EmiTooHigh { .. }));
    }

    #[test]
    fn low_income_protection() {
        let policy = EmiPolicy::low_income();
        let emi = 15_000.0;
        let income = 40_000.0;

        let err = is_emi_affordable(emi, income, &policy).unwrap_err();
        assert!(matches!(err, EmiError::EmiTooHigh { .. }));
    }

    #[test]
    fn calculate_emi_basic() {
        let emi = calculate_emi(1_000_000.0, 10.0, 240).unwrap();
        assert!(emi > 0.0);
    }

    #[test]
    fn custom_emi_policy_allows_edge() {
        let policy = EmiPolicy::custom(60.0, 10.0, IncomeType::Variable, false);

        let income = 50_000.0;
        let emi1 = 28_000.0; // allowed
        let emi2 = 31_000.0; // exceeds cap

        assert!(is_emi_affordable(emi1, income, &policy).is_ok());

        let err = is_emi_affordable(emi2, income, &policy).unwrap_err();
        assert!(matches!(err, EmiError::EmiTooHigh { .. }));
    }

    #[test]
    fn custom_emi_policy_surplus_check() {
        let policy = EmiPolicy::custom(75.0, 25.0, IncomeType::Salaried, false);

        let income = 40_000.0;
        let emi1 = 28_000.0; // allowed
        let emi2 = 32_000.0; // surplus violation

        assert!(is_emi_affordable(emi1, income, &policy).is_ok());

        let err = is_emi_affordable(emi2, income, &policy).unwrap_err();
        assert!(matches!(err, EmiError::EmiTooHigh { .. }));
    }

    #[test]
    fn custom_emi_policy_joint_borrower_flag() {
        let policy = EmiPolicy::custom(45.0, 20.0, IncomeType::Salaried, true);
        let income = 60_000.0;
        let emi = 25_000.0;

        assert!(policy.joint_borrowers);
        assert!(is_emi_affordable(emi, income, &policy).is_ok());
    }

    #[test]
    fn invalid_policy_rejection() {
        let policy = EmiPolicy::custom(90.0, 20.0, IncomeType::Salaried, false); // 90 + 20 > 100
        let income = 50_000.0;
        let emi = 40_000.0;

        let err = is_emi_affordable(emi, income, &policy).unwrap_err();
        assert!(matches!(err, EmiError::InvalidPolicy));
    }
}
