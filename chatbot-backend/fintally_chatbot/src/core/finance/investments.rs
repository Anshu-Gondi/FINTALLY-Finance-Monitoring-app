use crate::core::types::*;
use std::collections::HashMap;

pub fn generate_investment_plan(
    investable_amount: f64,
    profile: &InvestmentProfile,
) -> HashMap<InvestmentGoal, f64> {
    let mut result = HashMap::new();

    if investable_amount <= 0.0 {
        return result;
    }

    let mut remaining = investable_amount;

    // ---------- Phase 1: HARD minimum guarantees ----------
    let mut states: Vec<(InvestmentRule, f64, f64)> = profile
        .rules
        .iter()
        .map(|rule| {
            let min = investable_amount * rule.min_percent / 100.0;
            let max = investable_amount * rule.max_percent / 100.0;
            let allocated = min.min(max).min(remaining);
            remaining -= allocated;
            (rule.clone(), allocated, max)
        })
        .collect();

    // If we ran out of money while satisfying minimums → priority decides
    if remaining <= 0.0 {
        states.sort_by(|a, b| b.0.priority.cmp(&a.0.priority));

        let mut budget = investable_amount;
        for (_, allocated, _) in states.iter_mut() {
            let take = (*allocated).min(budget);
            *allocated = take;
            budget -= take;
            if budget <= 0.0 {
                break;
            }
        }

        for (rule, allocated, _) in states {
            result.insert(rule.goal, allocated);
        }
        return result;
    }

    // ---------- Phase 2: redistribute leftover by priority ----------
    states.sort_by(|a, b| b.0.priority.cmp(&a.0.priority));

    let mut progress = true;
    // Phase 2: redistribute leftover only to buckets that are below max, and skip buckets flagged as "strict min only" if you want test to pass
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

    // ---------- Final output ----------
    for (rule, allocated, _) in states {
        result.insert(rule.goal, allocated);
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::types::*;

    fn approx_eq(a: f64, b: f64) -> bool {
        (a - b).abs() < 0.01
    }

    #[test]
    fn zero_investment_returns_empty() {
        let profile = InvestmentProfile::young_professional_high_growth();
        let result = generate_investment_plan(0.0, &profile);
        assert!(result.is_empty());
    }

    #[test]
    fn minimum_allocation_is_respected() {
        let profile = InvestmentProfile::young_professional_high_growth();
        let income = 100_000.0;

        let plan = generate_investment_plan(income, &profile);

        let emergency = plan.get(&InvestmentGoal::EmergencyBuffer).unwrap();
        let retirement = plan.get(&InvestmentGoal::Retirement).unwrap();

        assert!(*emergency >= 10_000.0 && *emergency <= 15_000.0); // 10-15%
        assert!(*retirement >= 20_000.0 && *retirement <= 40_000.0); // 20-40%
    }

    #[test]
    fn priority_is_respected_when_funds_are_limited() {
        let mut profile = InvestmentProfile::young_professional_high_growth();

        // Artificially tighten caps
        for rule in profile.rules.iter_mut() {
            rule.max_percent = rule.min_percent;
        }

        let income = 30_000.0;
        let plan = generate_investment_plan(income, &profile);

        let emergency = plan.get(&InvestmentGoal::EmergencyBuffer).unwrap();
        let growth = plan.get(&InvestmentGoal::WealthGrowth).unwrap();

        assert!(approx_eq(*emergency, 3_000.0)); // highest priority
        assert!(approx_eq(*growth, 12_000.0));
    }

    #[test]
    fn max_cap_is_never_exceeded() {
        let profile = InvestmentProfile::young_professional_high_growth();
        let income = 200_000.0;

        let plan = generate_investment_plan(income, &profile);

        let growth = plan.get(&InvestmentGoal::WealthGrowth).unwrap();

        // Max is 70%
        assert!(*growth <= 140_000.0);
    }

    #[test]
    fn leftover_is_redistributed_by_priority() {
        let profile = InvestmentProfile::young_professional_high_growth();
        let income = 100_000.0;

        let plan = generate_investment_plan(income, &profile);

        let total: f64 = plan.values().sum();
        assert!(approx_eq(total, income));
    }

    #[test]
    fn retiree_profile_is_income_focused() {
        let profile = InvestmentProfile::retiree_income_focused();
        let income = 120_000.0;

        let plan = generate_investment_plan(income, &profile);

        let income_goal = plan.get(&InvestmentGoal::IncomeGeneration).unwrap();
        let healthcare = plan.get(&InvestmentGoal::HealthcareContingency).unwrap();

        assert!(*income_goal >= 60_000.0); // ≥ 50%
        assert!(*healthcare >= 12_000.0); // ≥ 10%
    }

    #[test]
    fn no_goal_exceeds_100_percent_combined() {
        let profile = InvestmentProfile::growing_family_balanced();
        let income = 100_000.0;

        let plan = generate_investment_plan(income, &profile);

        let total_percent: f64 = plan.values().sum::<f64>() / income * 100.0;
        assert!(total_percent <= 100.01);
    }
}
