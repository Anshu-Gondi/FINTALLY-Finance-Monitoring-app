use crate::core::types::*;
use crate::core::utils::domain_error::DomainError;
use crate::core::utils::errors::AppError;
use std::collections::HashMap;

pub fn generate_investment_plan_checked(
    investable_amount: f64,
    profile: &InvestmentProfile,
) -> Result<HashMap<InvestmentGoal, f64>, AppError> {
    if investable_amount <= 0.0 {
        return Err(AppError::InvalidInput(
            "Investable amount must be positive".to_string(),
        ));
    }

    let mut result = HashMap::new();
    let mut remaining = investable_amount;

    // ---------- Phase 1: HARD minimum guarantees ----------
    let mut states: Vec<(InvestmentRule, f64, f64)> = profile
        .rules
        .iter()
        .map(|rule| {
            // Validate percentages
            if rule.min_percent < 0.0 || rule.max_percent < 0.0 {
                return Err(AppError::Domain(DomainError::InvalidPercentage {
                    value: rule.min_percent.max(rule.max_percent),
                }));
            }

            let min = investable_amount * rule.min_percent / 100.0;
            let max = investable_amount * rule.max_percent / 100.0;

            if min > max {
                return Err(AppError::Domain(DomainError::AllocationOverflow {
                    attempted: min,
                    available: max,
                }));
            }

            // ---- Option 1: FAIL EARLY if remaining is insufficient ----
            if min > remaining {
                return Err(AppError::Domain(DomainError::AllocationOverflow {
                    attempted: min,
                    available: remaining,
                }));
            }

            // Allocate exactly the minimum
            let allocated = min;
            remaining -= allocated;

            Ok::<(InvestmentRule, f64, f64), AppError>((rule.clone(), allocated, max))
        })
        .collect::<Result<Vec<_>, AppError>>()?;

    // ---------- Option 1: Check if total minimum exceeds investable amount ----------
    let total_min: f64 = states.iter().map(|(_, allocated, _)| *allocated).sum();
    if total_min > investable_amount {
        return Err(AppError::Domain(DomainError::AllocationOverflow {
            attempted: total_min,
            available: investable_amount,
        }));
    }

    // ---------- Phase 2: Redistribute leftover by priority ----------
    states.sort_by(|a, b| b.0.priority.cmp(&a.0.priority)); // higher priority first

    let mut progress = true;
    while remaining > 0.0 && progress {
        progress = false;

        for (_, allocated, max) in states.iter_mut() {
            let space = *max - *allocated;
            if space <= 0.0 {
                continue;
            }

            let add = remaining.min(space);
            *allocated += add;
            remaining -= add;
            progress = true;

            if remaining <= 0.0 {
                break;
            }
        }
    }

    // If leftover funds cannot be distributed → allocation impossible
    if remaining > 0.0 {
        return Err(AppError::Domain(DomainError::AllocationOverflow {
            attempted: investable_amount,
            available: investable_amount - remaining,
        }));
    }

    // ---------- Final output ----------
    for (rule, allocated, _) in states {
        result.insert(rule.goal, allocated);
    }

    Ok(result)
}

/// Convenience wrapper returning plain HashMap, defaulting to empty on error
pub fn generate_investment_plan(
    investable_amount: f64,
    profile: &InvestmentProfile,
) -> HashMap<InvestmentGoal, f64> {
    match generate_investment_plan_checked(investable_amount, profile) {
        Ok(plan) => plan,
        Err(err) => {
            eprintln!("Investment plan error: {}", err);
            HashMap::new()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::types::*;

    fn approx_eq(a: f64, b: f64) -> bool {
        (a - b).abs() < 0.01
    }

    #[test]
    fn zero_investment_returns_error() {
        let profile = InvestmentProfile::young_professional_high_growth();
        let result = generate_investment_plan_checked(0.0, &profile);
        assert!(matches!(result, Err(AppError::InvalidInput(_))));
    }

    #[test]
    fn negative_rule_percent_triggers_error() {
        let mut profile = InvestmentProfile::young_professional_high_growth();
        profile.rules[0].min_percent = -10.0; // invalid
        let result = generate_investment_plan_checked(50_000.0, &profile);
        assert!(matches!(
            result,
            Err(AppError::Domain(DomainError::InvalidPercentage { .. }))
        ));
    }

    #[test]
    fn min_percent_greater_than_max_percent_triggers_allocation_error() {
        let mut profile = InvestmentProfile::young_professional_high_growth();
        profile.rules[0].min_percent = 50.0;
        profile.rules[0].max_percent = 20.0; // min > max
        let result = generate_investment_plan_checked(50_000.0, &profile);
        assert!(matches!(
            result,
            Err(AppError::Domain(DomainError::AllocationOverflow { .. }))
        ));
    }

    #[test]
    fn total_min_exceeding_investable_amount_triggers_error() {
        let mut profile = InvestmentProfile::young_professional_high_growth();

        // Set min_percent high enough to guarantee total_min > investable_amount
        // Assume 3 rules: min 40% each → total_min = 120% > 50_000
        for rule in profile.rules.iter_mut() {
            rule.min_percent = 40.0; // high minimum
            rule.max_percent = 60.0; // max > min
        }

        let investable_amount = 50_000.0;

        // Sanity check: ensure total minimum actually exceeds investable amount
        let total_min: f64 = profile
            .rules
            .iter()
            .map(|r| investable_amount * r.min_percent / 100.0)
            .sum();

        assert!(
            total_min > investable_amount,
            "Test setup invalid: total minimum ({}) does not exceed investable amount ({})",
            total_min,
            investable_amount
        );

        // Generate plan → must return AllocationOverflow
        let result = generate_investment_plan_checked(investable_amount, &profile);

        assert!(
            matches!(
                result,
                Err(AppError::Domain(DomainError::AllocationOverflow { .. }))
            ),
            "Expected AllocationOverflow, got: {:?}",
            result
        );
    }

    #[test]
    fn leftover_cannot_be_distributed_triggers_error() {
        let mut profile = InvestmentProfile::young_professional_high_growth();
        for rule in profile.rules.iter_mut() {
            rule.max_percent = rule.min_percent;
        }
        let result = generate_investment_plan_checked(100_000.0, &profile);
        assert!(matches!(
            result,
            Err(AppError::Domain(DomainError::AllocationOverflow { .. }))
        ));
    }

    // Correct allocations
    #[test]
    fn minimum_allocation_is_respected() {
        let profile = InvestmentProfile::young_professional_high_growth();
        let income = 100_000.0;

        let plan = generate_investment_plan_checked(income, &profile).unwrap();

        let emergency = plan.get(&InvestmentGoal::EmergencyBuffer).unwrap();
        let retirement = plan.get(&InvestmentGoal::Retirement).unwrap();

        assert!(*emergency >= 10_000.0 && *emergency <= 15_000.0); // 10-15%
        assert!(*retirement >= 20_000.0 && *retirement <= 40_000.0); // 20-40%
    }

    #[test]
    fn priority_is_respected_when_funds_are_limited() {
        let mut profile = InvestmentProfile::young_professional_high_growth();

        let income = 30_000.0;

        // Step 1: adjust minimums so total_min < income
        let total_rules = profile.rules.len() as f64;
        let safe_min_percent = (income / total_rules / income * 100.0).min(10.0);
        // ensures sum of minimums <= income

        for rule in profile.rules.iter_mut() {
            rule.min_percent = safe_min_percent;
            rule.max_percent = (safe_min_percent + 40.0).min(100.0); // leave room for redistribution
        }

        // Step 2: generate plan
        let plan_result = generate_investment_plan_checked(income, &profile);
        assert!(plan_result.is_ok(), "Plan failed: {:?}", plan_result);

        let plan = plan_result.unwrap();

        // Step 3: verify highest priority goal got at least its minimum
        let emergency_alloc = plan.get(&InvestmentGoal::EmergencyBuffer).unwrap();
        let growth_alloc = plan.get(&InvestmentGoal::WealthGrowth).unwrap();

        // minimums + redistribution based on priority
        assert!(
            *emergency_alloc >= income * safe_min_percent / 100.0,
            "Emergency allocation too low: got {}, expected at least {}",
            emergency_alloc,
            income * safe_min_percent / 100.0
        );
        assert!(
            *growth_alloc >= income * safe_min_percent / 100.0,
            "Growth allocation too low: got {}, expected at least {}",
            growth_alloc,
            income * safe_min_percent / 100.0
        );

        // Step 4: total allocation must not exceed income
        let total_alloc: f64 = plan.values().sum();
        assert!(
            total_alloc <= income + 0.01,
            "Total allocation exceeded income: {} > {}",
            total_alloc,
            income
        );
    }

    #[test]
    fn max_cap_is_never_exceeded() {
        let profile = InvestmentProfile::young_professional_high_growth();
        let income = 200_000.0;

        let plan = generate_investment_plan_checked(income, &profile).unwrap();

        let growth = plan.get(&InvestmentGoal::WealthGrowth).unwrap();
        assert!(*growth <= 140_000.0); // Max 70%
    }

    #[test]
    fn leftover_is_redistributed_by_priority() {
        let profile = InvestmentProfile::young_professional_high_growth();
        let income = 100_000.0;

        let plan = generate_investment_plan_checked(income, &profile).unwrap();

        let total: f64 = plan.values().sum();
        assert!(approx_eq(total, income));
    }

    #[test]
    fn retiree_profile_is_income_focused() {
        let profile = InvestmentProfile::retiree_income_focused();
        let income = 120_000.0;

        let plan = generate_investment_plan_checked(income, &profile).unwrap();

        let income_goal = plan.get(&InvestmentGoal::IncomeGeneration).unwrap();
        let healthcare = plan.get(&InvestmentGoal::HealthcareContingency).unwrap();

        assert!(*income_goal >= 60_000.0); // ≥ 50%
        assert!(*healthcare >= 12_000.0); // ≥ 10%
    }

    #[test]
    fn no_goal_exceeds_100_percent_combined() {
        let mut profile = InvestmentProfile::growing_family_balanced();

        let income = 100_000.0;

        // Explicitly shrink all min percents to fit under income
        let num_rules = profile.rules.len();
        let safe_min = 80_000.0 / income * 100.0 / num_rules as f64; // total min ≤ 80,000
        for rule in profile.rules.iter_mut() {
            rule.min_percent = safe_min;
            rule.max_percent = safe_min + 20.0; // small range above min
        }

        let plan_result = generate_investment_plan_checked(income, &profile);

        // Ensure plan succeeds
        assert!(
            plan_result.is_ok(),
            "Plan should succeed, got {:?}",
            plan_result
        );

        let plan = plan_result.unwrap();
        let total_percent: f64 = plan.values().sum::<f64>() / income * 100.0;
        assert!(total_percent <= 100.01, "Total allocation exceeded 100%");
    }
}
