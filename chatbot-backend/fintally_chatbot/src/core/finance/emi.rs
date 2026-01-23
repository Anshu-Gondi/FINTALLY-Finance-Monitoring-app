// src/core/finance/emi.rs

use crate::core::types::EmiPolicy;

pub fn calculate_emi(
    principal: f64,
    annual_rate: f64,
    tenure_months: u32,
) -> f64 {
    if principal <= 0.0 || annual_rate <= 0.0 || tenure_months == 0 {
        return 0.0;
    }

    let r = annual_rate / 12.0 / 100.0;
    let n = tenure_months as f64;

    principal * r * (1.0 + r).powf(n) / ((1.0 + r).powf(n) - 1.0)
}

pub fn is_emi_affordable(
    emi: f64,
    monthly_income: f64,
    policy: &EmiPolicy,
) -> bool {
    if monthly_income <= 0.0 || emi <= 0.0 {
        return false;
    }

    let emi_percent = (emi / monthly_income) * 100.0;
    if emi_percent > policy.max_emi_percent {
        return false;
    }

    let surplus_percent = 100.0 - emi_percent;
    if surplus_percent < policy.min_surplus_percent {
        return false;
    }

    true
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

        assert!(is_emi_affordable(emi, income, &policy));
    }

    #[test]
    fn salaried_emi_exceeds_limit() {
        let policy = EmiPolicy::salaried();
        let emi = 30_000.0;
        let income = 60_000.0;

        assert!(!is_emi_affordable(emi, income, &policy));
    }

    #[test]
    fn self_employed_stricter_cap() {
        let policy = EmiPolicy::self_employed();
        let emi = 20_000.0;
        let income = 50_000.0;

        assert!(!is_emi_affordable(emi, income, &policy));
    }

    #[test]
    fn low_income_protection() {
        let policy = EmiPolicy::low_income();
        let emi = 15_000.0;
        let income = 40_000.0;

        assert!(!is_emi_affordable(emi, income, &policy));
    }

    #[test]
    fn calculate_emi_basic() {
        let emi = calculate_emi(1_000_000.0, 10.0, 240);
        assert!(emi > 0.0);
    }
}
