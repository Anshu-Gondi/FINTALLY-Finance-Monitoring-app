// src/core/finance/emi.rs

use crate::core::types::*;

pub fn calculate_emi(principal: f64, annual_rate: f64, tenure_months: u32) -> f64 {
    if principal <= 0.0 || annual_rate <= 0.0 || tenure_months == 0 {
        return 0.0;
    }

    let r = annual_rate / 12.0 / 100.0;
    let n = tenure_months as f64;

    principal * r * (1.0 + r).powf(n) / ((1.0 + r).powf(n) - 1.0)
}

pub fn is_emi_affordable(emi: f64, monthly_income: f64, policy: &EmiPolicy) -> bool {
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

    #[test]
    fn custom_emi_policy_allows_edge() {
        // Max EMI 60%, min surplus 10%
        let policy = EmiPolicy::custom(60.0, 10.0, IncomeType::Variable, false);

        let income = 50_000.0;
        let emi1 = 28_000.0; // 56% → allowed
        let emi2 = 31_000.0; // 62% → exceeds max_emi_percent

        assert!(is_emi_affordable(emi1, income, &policy));
        assert!(!is_emi_affordable(emi2, income, &policy));
    }

    #[test]
    fn custom_emi_policy_surplus_check() {
        // Adjusted max EMI so EMI1 is allowed
        let policy = EmiPolicy::custom(75.0, 25.0, IncomeType::Salaried, false);

        let income = 40_000.0;
        let emi1 = 28_000.0; // 70% → surplus 30% → allowed
        let emi2 = 32_000.0; // 80% → surplus 20% → fails

        assert!(is_emi_affordable(emi1, income, &policy));
        assert!(!is_emi_affordable(emi2, income, &policy));
    }

    #[test]
    fn custom_emi_policy_joint_borrower_flag() {
        let policy = EmiPolicy::custom(45.0, 20.0, IncomeType::Salaried, true);
        let income = 60_000.0;
        let emi = 25_000.0; // 41.6% EMI → allowed

        assert!(policy.joint_borrowers);
        assert!(is_emi_affordable(emi, income, &policy));
    }
}
