use crate::core::types::{BudgetProfile, BudgetCategory};
use std::collections::HashMap;

#[derive(Debug)]
struct AllocationState {
    allocated: f64,
    max_allowed: f64,
    priority: u8,
}

pub fn generate_budget(
    monthly_income: f64,
    profile: &BudgetProfile,
) -> HashMap<BudgetCategory, f64> {
    let mut result = HashMap::new();

    if monthly_income <= 0.0 {
        return result;
    }

    let mut remaining = monthly_income;

    // Phase 1: allocate minimums
    let mut states: Vec<(BudgetCategory, AllocationState)> = profile
        .rules
        .iter()
        .map(|rule| {
            let min = monthly_income * rule.min_percent / 100.0;
            let max = monthly_income * rule.max_percent / 100.0;

            let allocated = min.min(remaining);
            remaining -= allocated;

            (
                rule.category.clone(),
                AllocationState {
                    allocated,
                    max_allowed: max,
                    priority: rule.priority,
                },
            )
        })
        .collect();

    // Phase 2: redistribute leftover by priority
    states.sort_by(|a, b| b.1.priority.cmp(&a.1.priority));

    let mut progress = true;
    while remaining > 0.0 && progress {
        progress = false;

        for (_, state) in states.iter_mut() {
            if remaining <= 0.0 {
                break;
            }

            let available_space = state.max_allowed - state.allocated;
            if available_space <= 0.0 {
                continue;
            }

            let increment = remaining.min(available_space);
            state.allocated += increment;
            remaining -= increment;
            progress = true;
        }
    }

    // Final output
    for (category, state) in states {
        result.insert(category, state.allocated);
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::types::{BudgetProfile, BudgetCategory};

    fn approx_eq(a: f64, b: f64) -> bool {
        (a - b).abs() < 0.0001
    }

    #[test]
    fn zero_income_returns_empty_budget() {
        let profile = BudgetProfile::single_young_professional();
        let budget = generate_budget(0.0, &profile);
        assert!(budget.is_empty());
    }

    #[test]
    fn respects_minimum_percentages() {
        let profile = BudgetProfile::single_young_professional();
        let income = 100_000.0;

        let budget = generate_budget(income, &profile);

        let housing = budget.get(&BudgetCategory::Housing).unwrap();
        let savings = budget.get(&BudgetCategory::Savings).unwrap();

        assert!(housing >= &(income * 0.25));
        assert!(savings >= &(income * 0.15));
    }

    #[test]
    fn respects_maximum_caps() {
        let profile = BudgetProfile::single_young_professional();
        let income = 1_000_000.0;

        let budget = generate_budget(income, &profile);

        let housing = budget.get(&BudgetCategory::Housing).unwrap();
        let lifestyle = budget.get(&BudgetCategory::Lifestyle).unwrap();

        assert!(housing <= &(income * 0.35));
        assert!(lifestyle <= &(income * 0.15));
    }

    #[test]
    fn leftover_is_redistributed_by_priority() {
        let profile = BudgetProfile::single_young_professional();
        let income = 200_000.0;

        let budget = generate_budget(income, &profile);

        let savings = budget.get(&BudgetCategory::Savings).unwrap();
        let lifestyle = budget.get(&BudgetCategory::Lifestyle).unwrap();

        // Savings has higher priority than lifestyle
        assert!(savings > lifestyle);
    }

    #[test]
    fn total_budget_never_exceeds_income() {
        let profile = BudgetProfile::single_parent();
        let income = 150_000.0;

        let budget = generate_budget(income, &profile);
        let total: f64 = budget.values().sum();

        assert!(total <= income);
        assert!(approx_eq(total, income) || total < income);
    }

    #[test]
    fn emergency_fund_is_prioritized_for_single_parent() {
        let profile = BudgetProfile::single_parent();
        let income = 120_000.0;

        let budget = generate_budget(income, &profile);

        let emergency = budget
            .get(&BudgetCategory::EmergencyFund)
            .unwrap();
        let education = budget
            .get(&BudgetCategory::Education)
            .unwrap();

        // Emergency fund has higher priority than education
        assert!(emergency >= education);
    }
}
