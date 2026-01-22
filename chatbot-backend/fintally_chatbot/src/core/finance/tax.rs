use crate::core::types::*;
use std::collections::HashMap;

pub fn calculate_tax(
    amount: f64,
    profile: &TaxProfile,
) -> HashMap<TaxDomain, f64> {
    let mut result = HashMap::new();

    if amount <= 0.0 {
        return result;
    }

    let mut rules = profile.rules.clone();
    rules.sort_by(|a, b| b.priority.cmp(&a.priority));

    for rule in rules {
        if !rule.enabled {
            continue;
        }

        let tax = match rule.base {
            TaxBase::FlatAmount(v) => v,
            TaxBase::PercentageOfIncome => amount * rule.rate_percent / 100.0,
            TaxBase::PercentageOfAmount => amount * rule.rate_percent / 100.0,
        };

        result.insert(rule.domain.clone(), tax);
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::types::*;

    fn approx_eq(a: f64, b: f64) -> bool {
        (a - b).abs() < 1e-6
    }

    #[test]
    fn zero_income_returns_empty() {
        let profile = TaxProfile {
            rules: vec![],
        };

        let taxes = calculate_tax(0.0, &profile);
        assert!(taxes.is_empty());
    }

    #[test]
    fn single_income_tax_percentage() {
        let profile = TaxProfile {
            rules: vec![
                TaxRule {
                    domain: TaxDomain::Income,
                    rate_percent: 10.0,
                    base: TaxBase::PercentageOfIncome,
                    priority: 10,
                    enabled: true,
                }
            ],
        };

        let taxes = calculate_tax(100_000.0, &profile);

        assert!(approx_eq(
            *taxes.get(&TaxDomain::Income).unwrap(),
            10_000.0
        ));
    }

    #[test]
    fn flat_amount_tax() {
        let profile = TaxProfile {
            rules: vec![
                TaxRule {
                    domain: TaxDomain::Funeral,
                    rate_percent: 0.0,
                    base: TaxBase::FlatAmount(5_000.0),
                    priority: 5,
                    enabled: true,
                }
            ],
        };

        let taxes = calculate_tax(50_000.0, &profile);

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

        let taxes = calculate_tax(100_000.0, &profile);

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
            rules: vec![
                TaxRule {
                    domain: TaxDomain::Income,
                    rate_percent: 10.0,
                    base: TaxBase::PercentageOfIncome,
                    priority: 10,
                    enabled: false,
                }
            ],
        };

        let taxes = calculate_tax(100_000.0, &profile);
        assert!(taxes.is_empty());
    }

    #[test]
    fn custom_tax_domain_supported() {
        let profile = TaxProfile {
            rules: vec![
                TaxRule {
                    domain: TaxDomain::Custom("WealthTax".into()),
                    rate_percent: 2.0,
                    base: TaxBase::PercentageOfAmount,
                    priority: 10,
                    enabled: true,
                }
            ],
        };

        let taxes = calculate_tax(1_000_000.0, &profile);

        assert!(approx_eq(
            *taxes
                .get(&TaxDomain::Custom("WealthTax".into()))
                .unwrap(),
            20_000.0
        ));
    }
}
