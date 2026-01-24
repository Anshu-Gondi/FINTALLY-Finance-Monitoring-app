use crate::core::types::{BudgetCategory, BudgetProfile};
use crate::core::utils::errors::AppError;
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
) -> Result<HashMap<BudgetCategory, f64>, AppError> {
    if monthly_income <= 0.0 {
        return Err(AppError::InvalidInput(
            "Monthly income must be greater than zero".into(),
        ));
    }

    if profile.rules.is_empty() {
        return Err(AppError::ProfileNotFound(
            "Budget profile has no rules".into(),
        ));
    }

    // ---------- Validate rules ----------
    let total_min: f64 = profile.rules.iter().map(|r| r.min_percent).sum();

    if total_min > 100.0 {
        return Err(AppError::AllocationError(format!(
            "Total minimum allocation exceeds 100% (got {:.2}%)",
            total_min
        )));
    }

    for rule in &profile.rules {
        if rule.min_percent > rule.max_percent {
            return Err(AppError::InvalidInput(format!(
                "Min percent exceeds max percent for {:?}",
                rule.category
            )));
        }
    }

    let mut remaining = monthly_income;

    // ---------- Phase 1: allocate minimums ----------
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

    // ---------- Phase 2: redistribute leftover by priority ----------
    states.sort_by(|a, b| b.1.priority.cmp(&a.1.priority));

    let mut progressed = true;
    while remaining > 0.0 && progressed {
        progressed = false;

        for (_, state) in states.iter_mut() {
            let space = state.max_allowed - state.allocated;
            if space <= 0.0 {
                continue;
            }

            let add = remaining.min(space);
            state.allocated += add;
            remaining -= add;
            progressed = true;

            if remaining <= 0.0 {
                break;
            }
        }
    }

    // ---------- Final output ----------
    let mut result = HashMap::new();
    let total_allocated: f64 = states.iter().map(|(_, s)| s.allocated).sum();

    if total_allocated > monthly_income + 0.01 {
        return Err(AppError::CalculationError(
            "Final allocation exceeds income".into(),
        ));
    }

    for (category, state) in states {
        result.insert(category, state.allocated);
    }

    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::types::{BudgetCategory, BudgetProfile};

    fn approx_eq(a: f64, b: f64) -> bool {
        (a - b).abs() < 0.0001
    }

    #[test]
    fn zero_income_returns_error() {
        let profile = BudgetProfile::single_young_professional();
        let budget = generate_budget(0.0, &profile);
        assert!(budget.is_err());
    }

    #[test]
    fn respects_minimum_percentages() {
        let profile = BudgetProfile::single_young_professional();
        let income = 100_000.0;

        let budget = generate_budget(income, &profile).unwrap();

        let housing = budget.get(&BudgetCategory::Housing).unwrap();
        let savings = budget.get(&BudgetCategory::Savings).unwrap();

        assert!(*housing >= income * 0.25);
        assert!(*savings >= income * 0.15);
    }

    #[test]
    fn respects_maximum_caps() {
        let profile = BudgetProfile::single_young_professional();
        let income = 1_000_000.0;

        let budget = generate_budget(income, &profile).unwrap();

        let housing = budget.get(&BudgetCategory::Housing).unwrap();
        let lifestyle = budget.get(&BudgetCategory::Lifestyle).unwrap();

        assert!(*housing <= income * 0.35);
        assert!(*lifestyle <= income * 0.15);
    }

    #[test]
    fn leftover_is_redistributed_by_priority() {
        let profile = BudgetProfile::single_young_professional();
        let income = 200_000.0;

        let budget = generate_budget(income, &profile).unwrap();

        let savings = budget.get(&BudgetCategory::Savings).unwrap();
        let lifestyle = budget.get(&BudgetCategory::Lifestyle).unwrap();

        assert!(savings > lifestyle);
    }

    #[test]
    fn total_budget_never_exceeds_income() {
        let profile = BudgetProfile::single_parent();
        let income = 150_000.0;

        let budget = generate_budget(income, &profile).unwrap();
        let total: f64 = budget.values().sum();

        assert!(total <= income);
        assert!(approx_eq(total, income) || total < income);
    }

    #[test]
    fn emergency_fund_is_prioritized_for_single_parent() {
        let profile = BudgetProfile::single_parent();
        let income = 120_000.0;

        let budget = generate_budget(income, &profile).unwrap();

        let emergency = budget.get(&BudgetCategory::EmergencyFund).unwrap();
        let education = budget.get(&BudgetCategory::Education).unwrap();

        assert!(emergency >= education);
    }
}
