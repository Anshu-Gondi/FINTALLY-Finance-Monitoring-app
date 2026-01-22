// src/core/finance/savings.rs

use crate::core::types::{EmergencyFundPolicy, SavingsPolicy};

pub fn emergency_fund(
    monthly_expense: f64,
    policy: &EmergencyFundPolicy,
) -> f64 {
    monthly_expense * policy.months * policy.expense_multiplier
}

pub fn savings_projection(
    months: u32,
    policy: &SavingsPolicy,
) -> f64 {
    let monthly_rate = policy.annual_growth_rate / 12.0;
    let mut total = 0.0;

    for _ in 0..months {
        total = total * (1.0 + monthly_rate) + policy.monthly_contribution;
    }

    total
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::config::FinanceProfile;

    #[test]
    fn emergency_fund_default_profile() {
        let profile = FinanceProfile::default();
        let fund = emergency_fund(20_000.0, &profile.emergency_fund);
        assert_eq!(fund, 120_000.0);
    }

    #[test]
    fn emergency_fund_millionaire_profile() {
        let profile = FinanceProfile::millionaire();
        let fund = emergency_fund(100_000.0, &profile.emergency_fund);
        assert_eq!(fund, 100_000.0);
    }
}
