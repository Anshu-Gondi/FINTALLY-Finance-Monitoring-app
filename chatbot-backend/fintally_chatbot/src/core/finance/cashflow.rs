use crate::core::types::{CashflowProfile, CashflowBucket};
use std::collections::HashMap;

#[derive(Debug)]
struct AllocationState {
    allocated: f64,
    max_allowed: f64,
    priority: u8,
}

pub fn generate_cashflow(
    monthly_income: f64,
    profile: &CashflowProfile,
) -> HashMap<CashflowBucket, f64> {
    let mut result = HashMap::new();

    if monthly_income <= 0.0 {
        return result;
    }

    let mut remaining = monthly_income;

    // Phase 1: minimum guarantees
    let mut states: Vec<(CashflowBucket, AllocationState)> = profile
        .rules
        .iter()
        .map(|rule| {
            let min = monthly_income * rule.min_percent / 100.0;
            let max = monthly_income * rule.max_percent / 100.0;

            let allocated = min.min(remaining);
            remaining -= allocated;

            (
                rule.bucket.clone(),
                AllocationState {
                    allocated,
                    max_allowed: max,
                    priority: rule.priority,
                },
            )
        })
        .collect();

    // Phase 2: redistribute remaining by priority
    states.sort_by(|a, b| b.1.priority.cmp(&a.1.priority));

    let mut progress = true;
    while remaining > 0.0 && progress {
        progress = false;

        for (_, state) in states.iter_mut() {
            let space = state.max_allowed - state.allocated;
            if space <= 0.0 || remaining <= 0.0 {
                continue;
            }

            let increment = remaining.min(space);
            state.allocated += increment;
            remaining -= increment;
            progress = true;
        }
    }

    for (bucket, state) in states {
        result.insert(bucket, state.allocated);
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::types::{CashflowBucket, CashflowProfile};

    fn sum(values: &std::collections::HashMap<CashflowBucket, f64>) -> f64 {
        values.values().sum()
    }

    #[test]
    fn zero_income_returns_empty() {
        let profile = CashflowProfile::fifty_thirty_twenty();
        let result = generate_cashflow(0.0, &profile);
        assert!(result.is_empty());
    }

    #[test]
    fn fifty_thirty_twenty_basic_split() {
        let profile = CashflowProfile::fifty_thirty_twenty();
        let income = 100_000.0;

        let result = generate_cashflow(income, &profile);

        assert_eq!(sum(&result), income);

        assert_eq!(
            result.get(&CashflowBucket::Essentials).unwrap(),
            &50_000.0
        );
        assert_eq!(
            result.get(&CashflowBucket::FinancialStability).unwrap(),
            &20_000.0
        );
        assert_eq!(
            result.get(&CashflowBucket::Lifestyle).unwrap(),
            &30_000.0
        );
    }

    #[test]
    fn respects_max_caps() {
        let profile = CashflowProfile::fifty_thirty_twenty();
        let income = 200_000.0;

        let result = generate_cashflow(income, &profile);

        let essentials = result.get(&CashflowBucket::Essentials).unwrap();
        let stability = result.get(&CashflowBucket::FinancialStability).unwrap();
        let lifestyle = result.get(&CashflowBucket::Lifestyle).unwrap();

        // Max caps enforced
        assert!(*essentials <= 120_000.0); // 60%
        assert!(*stability <= 60_000.0);   // 30%
        assert!(*lifestyle <= 60_000.0);   // 30%

        assert_eq!(sum(&result), income);
    }

    #[test]
    fn redistributes_leftover_by_priority() {
        let mut profile = CashflowProfile::fifty_thirty_twenty();

        // Artificially tighten lifestyle cap
        for rule in profile.rules.iter_mut() {
            if rule.bucket == CashflowBucket::Lifestyle {
                rule.max_percent = 10.0;
            }
        }

        let income = 100_000.0;
        let result = generate_cashflow(income, &profile);

        let lifestyle = result.get(&CashflowBucket::Lifestyle).unwrap();
        let essentials = result.get(&CashflowBucket::Essentials).unwrap();
        let stability = result.get(&CashflowBucket::FinancialStability).unwrap();

        // Lifestyle capped
        assert_eq!(*lifestyle, 10_000.0);

        // Remaining redistributed to higher priority buckets
        assert!(*essentials > 50_000.0);
        assert!(*stability > 20_000.0);

        assert_eq!(sum(&result), income);
    }

    #[test]
    fn student_profile_prioritizes_essentials() {
        let profile = CashflowProfile::student();
        let income = 50_000.0;

        let result = generate_cashflow(income, &profile);

        let essentials = result.get(&CashflowBucket::Essentials).unwrap();
        let lifestyle = result.get(&CashflowBucket::Lifestyle).unwrap();

        assert!(*essentials >= 30_000.0); // ≥60%
        assert!(*lifestyle <= 10_000.0);  // ≤20%

        assert_eq!(sum(&result), income);
    }

    #[test]
    fn family_profile_limits_lifestyle_first() {
        let profile = CashflowProfile::family();
        let income = 120_000.0;

        let result = generate_cashflow(income, &profile);

        let lifestyle = result.get(&CashflowBucket::Lifestyle).unwrap();
        let essentials = result.get(&CashflowBucket::Essentials).unwrap();

        // Lifestyle deliberately constrained
        assert!(*lifestyle <= 18_000.0); // ≤15%

        // Essentials dominate
        assert!(*essentials >= 78_000.0); // ≥65%

        assert_eq!(sum(&result), income);
    }
}
