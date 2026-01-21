// src/core/finance/savings.rs

/// Calculates recommended emergency fund amount
/// Rule: 6 months of expenses
pub fn emergency_fund(monthly_expense: f64) -> f64 {
    monthly_expense * 6.0
}

/// Projects savings over time with fixed monthly contribution
pub fn savings_projection(
    monthly_saving: f64,
    months: u32,
) -> f64 {
    monthly_saving * months as f64
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn emergency_fund_unit_test() {
        let monthly = 20_000.0;
        let fund = emergency_fund(monthly);
        assert_eq!(fund, 120_000.0);
    }

    #[test]
    fn savings_projection_unit_test() {
        let total = savings_projection(5_000.0, 12);
        assert_eq!(total, 60_000.0);
    }
}
