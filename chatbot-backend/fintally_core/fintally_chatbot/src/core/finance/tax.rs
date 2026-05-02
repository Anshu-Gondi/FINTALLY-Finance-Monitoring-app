use crate::core::types::*;
use crate::core::utils::domain_error::DomainError;
use std::collections::HashMap;

/// Calculate taxes with strict validation
pub fn calculate_tax(
    amount: f64,
    profile: &TaxProfile,
) -> Result<HashMap<TaxDomain, f64>, DomainError> {
    if amount <= 0.0 {
        return Err(DomainError::InvalidAmount { value: amount });
    }

    if profile.rules.is_empty() {
        return Err(DomainError::ProfileInvariantViolated {
            reason: "Tax profile has no rules".to_string(),
        });
    }

    let mut result = HashMap::new();
    let mut rules = profile.rules.clone();

    // Sort by priority descending
    rules.sort_by(|a, b| b.priority.cmp(&a.priority));

    for rule in rules {
        if !rule.enabled {
            continue;
        }

        match rule.base {
            TaxBase::PercentageOfIncome | TaxBase::PercentageOfAmount => {
                if rule.rate_percent < 0.0 {
                    return Err(DomainError::InvalidPercentage {
                        value: rule.rate_percent,
                    });
                }
            }
            TaxBase::FlatAmount(v) => {
                if v < 0.0 {
                    return Err(DomainError::InvalidAmount { value: v });
                }
            }
        }

        let tax = match rule.base {
            TaxBase::FlatAmount(v) => v,
            TaxBase::PercentageOfIncome | TaxBase::PercentageOfAmount => {
                amount * rule.rate_percent / 100.0
            }
        };

        *result.entry(rule.domain.clone()).or_insert(0.0) += tax;
    }

    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::utils::domain_error::DomainError;

    fn approx_eq(a: f64, b: f64) -> bool {
        (a - b).abs() < 1e-6
    }

    #[test]
    fn zero_amount_returns_error() {
        let profile = TaxProfile { rules: vec![] };

        let result = calculate_tax(0.0, &profile);

        assert!(matches!(
            result,
            Err(DomainError::InvalidAmount { value }) if value == 0.0
        ));
    }

    #[test]
    fn empty_profile_returns_error() {
        let profile = TaxProfile { rules: vec![] };

        let result = calculate_tax(100_000.0, &profile);

        assert!(matches!(
            result,
            Err(DomainError::ProfileInvariantViolated { .. })
        ));
    }

    #[test]
    fn negative_tax_rate_returns_error() {
        let profile = TaxProfile {
            rules: vec![TaxRule {
                domain: TaxDomain::Income,
                rate_percent: -5.0,
                base: TaxBase::PercentageOfIncome,
                priority: 10,
                enabled: true,
            }],
        };

        let result = calculate_tax(100_000.0, &profile);

        assert!(matches!(
            result,
            Err(DomainError::InvalidPercentage { value }) if value == -5.0
        ));
    }

    #[test]
    fn negative_flat_amount_returns_error() {
        let profile = TaxProfile {
            rules: vec![TaxRule {
                domain: TaxDomain::Custom("Funeral".into()),
                rate_percent: 0.0,
                base: TaxBase::FlatAmount(-1000.0),
                priority: 5,
                enabled: true,
            }],
        };

        let result = calculate_tax(50_000.0, &profile);

        assert!(matches!(
            result,
            Err(DomainError::InvalidAmount { value }) if value == -1000.0
        ));
    }

    #[test]
    fn single_income_tax_percentage() {
        let profile = TaxProfile {
            rules: vec![TaxRule {
                domain: TaxDomain::Income,
                rate_percent: 10.0,
                base: TaxBase::PercentageOfIncome,
                priority: 10,
                enabled: true,
            }],
        };

        let taxes = calculate_tax(100_000.0, &profile).unwrap();

        assert!(approx_eq(
            *taxes.get(&TaxDomain::Income).unwrap(),
            10_000.0
        ));
    }

    #[test]
    fn flat_amount_tax() {
        let profile = TaxProfile {
            rules: vec![TaxRule {
                domain: TaxDomain::Funeral,
                rate_percent: 0.0,
                base: TaxBase::FlatAmount(5_000.0),
                priority: 5,
                enabled: true,
            }],
        };

        let taxes = calculate_tax(50_000.0, &profile).unwrap();

        assert!(approx_eq(
            *taxes.get(&TaxDomain::Funeral).unwrap(),
            5_000.0
        ));
    }

    #[test]
    fn multiple_taxes_applied() {
        let profile = TaxProfile {
            rules: vec![
                TaxRule {
                    domain: TaxDomain::Income,
                    rate_percent: 10.0,
                    base: TaxBase::PercentageOfIncome,
                    priority: 10,
                    enabled: true,
                },
                TaxRule {
                    domain: TaxDomain::Insurance,
                    rate_percent: 5.0,
                    base: TaxBase::PercentageOfAmount,
                    priority: 5,
                    enabled: true,
                },
            ],
        };

        let taxes = calculate_tax(100_000.0, &profile).unwrap();

        assert!(approx_eq(
            *taxes.get(&TaxDomain::Income).unwrap(),
            10_000.0
        ));
        assert!(approx_eq(
            *taxes.get(&TaxDomain::Insurance).unwrap(),
            5_000.0
        ));
    }

    #[test]
    fn disabled_tax_is_ignored() {
        let profile = TaxProfile {
            rules: vec![TaxRule {
                domain: TaxDomain::Income,
                rate_percent: 10.0,
                base: TaxBase::PercentageOfIncome,
                priority: 10,
                enabled: false,
            }],
        };

        let taxes = calculate_tax(100_000.0, &profile).unwrap();

        assert!(taxes.is_empty());
    }
}
