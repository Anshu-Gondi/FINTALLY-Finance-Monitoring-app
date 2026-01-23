// src/core/config.rs

use crate::core::types::*;

/*
   Default Implementations and Predefined Profiles for finance configurations
   includes Files: savings.rs, budgeting.rs, cashflow.rs, investments.rs, emi.rs, loans.rs, tax.rs
   Includes:
   - Default trait implementations for policy structs
   - Predefined profiles for FinanceProfile, BudgetProfile, CashflowProfile, InvestmentProfile, TaxProfile, LoanPolicy, EmiPolicy
*/

// Emergency Fund Policy Default

impl Default for EmergencyFundPolicy {
    fn default() -> Self {
        Self {
            months: 6.0,
            expense_multiplier: 1.0,
        }
    }
}

// Savings Policy Default

impl Default for SavingsPolicy {
    fn default() -> Self {
        Self {
            monthly_contribution: 0.0,
            annual_growth_rate: 0.0,
        }
    }
}

// Finance Profile Default

impl Default for FinanceProfile {
    fn default() -> Self {
        Self {
            emergency_fund: EmergencyFundPolicy::default(),
            savings: SavingsPolicy::default(),
        }
    }
}

// Finance Side Profiles

impl FinanceProfile {
    pub fn millionaire() -> Self {
        Self {
            emergency_fund: EmergencyFundPolicy {
                months: 1.0,
                expense_multiplier: 1.0,
            },
            savings: SavingsPolicy {
                monthly_contribution: 0.0,
                annual_growth_rate: 0.0,
            },
        }
    }

    pub fn conservative() -> Self {
        Self {
            emergency_fund: EmergencyFundPolicy {
                months: 12.0,
                expense_multiplier: 1.2,
            },
            savings: SavingsPolicy {
                monthly_contribution: 0.0,
                annual_growth_rate: 0.04,
            },
        }
    }
}

// Budget Side Profiles

impl BudgetProfile {
    pub fn single_young_professional() -> Self {
        Self {
            rules: vec![
                BudgetRule {
                    category: BudgetCategory::Housing,
                    min_percent: 25.0,
                    max_percent: 35.0,
                    priority: 10,
                },
                BudgetRule {
                    category: BudgetCategory::Food,
                    min_percent: 10.0,
                    max_percent: 15.0,
                    priority: 8,
                },
                BudgetRule {
                    category: BudgetCategory::Transportation,
                    min_percent: 5.0,
                    max_percent: 10.0,
                    priority: 7,
                },
                BudgetRule {
                    category: BudgetCategory::Savings,
                    min_percent: 15.0,
                    max_percent: 30.0,
                    priority: 9,
                },
                BudgetRule {
                    category: BudgetCategory::Lifestyle,
                    min_percent: 5.0,
                    max_percent: 15.0,
                    priority: 4,
                },
            ],
        }
    }

    pub fn single_parent() -> Self {
        Self {
            rules: vec![
                BudgetRule {
                    category: BudgetCategory::Housing,
                    min_percent: 30.0,
                    max_percent: 40.0,
                    priority: 10,
                },
                BudgetRule {
                    category: BudgetCategory::Childcare,
                    min_percent: 10.0,
                    max_percent: 20.0,
                    priority: 9,
                },
                BudgetRule {
                    category: BudgetCategory::Education,
                    min_percent: 5.0,
                    max_percent: 10.0,
                    priority: 8,
                },
                BudgetRule {
                    category: BudgetCategory::Healthcare,
                    min_percent: 5.0,
                    max_percent: 10.0,
                    priority: 9,
                },
                BudgetRule {
                    category: BudgetCategory::EmergencyFund,
                    min_percent: 10.0,
                    max_percent: 20.0,
                    priority: 10,
                },
            ],
        }
    }

    pub fn couple_with_dependents() -> Self {
        Self {
            rules: vec![
                BudgetRule {
                    category: BudgetCategory::Housing,
                    min_percent: 30.0,
                    max_percent: 40.0,
                    priority: 10,
                },
                BudgetRule {
                    category: BudgetCategory::Food,
                    min_percent: 15.0,
                    max_percent: 20.0,
                    priority: 9,
                },
                BudgetRule {
                    category: BudgetCategory::Healthcare,
                    min_percent: 8.0,
                    max_percent: 12.0,
                    priority: 9,
                },
                BudgetRule {
                    category: BudgetCategory::Savings,
                    min_percent: 10.0,
                    max_percent: 25.0,
                    priority: 8,
                },
                BudgetRule {
                    category: BudgetCategory::Education,
                    min_percent: 5.0,
                    max_percent: 15.0,
                    priority: 7,
                },
            ],
        }
    }

    pub fn apply_rural_adjustments(&mut self) {
        for rule in &mut self.rules {
            if rule.category == BudgetCategory::Transportation {
                rule.max_percent += 5.0;
            }
        }
    }
}

// Cashflow Side Profiles

impl CashflowProfile {
    /// Classic 50-30-20 rule (STRICT, no redistribution)
    pub fn fifty_thirty_twenty() -> Self {
        Self {
            mode: CashflowMode::FixedRatio,
            rules: vec![
                CashflowRule {
                    bucket: CashflowBucket::Essentials,
                    min_percent: 50.0,
                    max_percent: 50.0, // 🔒 fixed
                    priority: 0,
                },
                CashflowRule {
                    bucket: CashflowBucket::FinancialStability,
                    min_percent: 20.0,
                    max_percent: 20.0,
                    priority: 0,
                },
                CashflowRule {
                    bucket: CashflowBucket::Lifestyle,
                    min_percent: 30.0,
                    max_percent: 30.0,
                    priority: 0,
                },
            ],
        }
    }

    /// Flexible, priority-based
    pub fn student() -> Self {
        Self {
            mode: CashflowMode::PriorityBased,
            rules: vec![
                CashflowRule {
                    bucket: CashflowBucket::Essentials,
                    min_percent: 60.0,
                    max_percent: 70.0,
                    priority: 10,
                },
                CashflowRule {
                    bucket: CashflowBucket::FinancialStability,
                    min_percent: 10.0,
                    max_percent: 20.0,
                    priority: 8,
                },
                CashflowRule {
                    bucket: CashflowBucket::Lifestyle,
                    min_percent: 10.0,
                    max_percent: 20.0,
                    priority: 4,
                },
            ],
        }
    }

    pub fn family() -> Self {
        Self {
            mode: CashflowMode::PriorityBased,
            rules: vec![
                CashflowRule {
                    bucket: CashflowBucket::Essentials,
                    min_percent: 65.0,
                    max_percent: 75.0,
                    priority: 10,
                },
                CashflowRule {
                    bucket: CashflowBucket::FinancialStability,
                    min_percent: 15.0,
                    max_percent: 25.0,
                    priority: 9,
                },
                CashflowRule {
                    bucket: CashflowBucket::Lifestyle,
                    min_percent: 5.0,
                    max_percent: 15.0,
                    priority: 3,
                },
            ],
        }
    }

    pub fn young_professional() -> Self {
        Self {
            mode: CashflowMode::PriorityBased,
            rules: vec![
                CashflowRule {
                    bucket: CashflowBucket::Essentials,
                    min_percent: 45.0,
                    max_percent: 55.0,
                    priority: 10,
                },
                CashflowRule {
                    bucket: CashflowBucket::FinancialStability,
                    min_percent: 25.0,
                    max_percent: 35.0,
                    priority: 9,
                },
                CashflowRule {
                    bucket: CashflowBucket::Lifestyle,
                    min_percent: 15.0,
                    max_percent: 25.0,
                    priority: 6,
                },
            ],
        }
    }
}

// Investment Side Profiles

impl InvestmentProfile {
    pub fn young_professional_high_growth() -> Self {
        Self {
            life_stage: LifeStage::YoungProfessional,
            risk_tolerance: RiskTolerance::High,
            rules: vec![
                InvestmentRule {
                    goal: InvestmentGoal::EmergencyBuffer,
                    min_percent: 10.0,
                    max_percent: 15.0,
                    priority: 10,
                    allocation: vec![AssetAllocation {
                        asset: AssetClass::Cash,
                        percent: 100.0,
                    }],
                },
                InvestmentRule {
                    goal: InvestmentGoal::WealthGrowth,
                    min_percent: 40.0,
                    max_percent: 70.0,
                    priority: 9,
                    allocation: vec![
                        AssetAllocation {
                            asset: AssetClass::Equity,
                            percent: 80.0,
                        },
                        AssetAllocation {
                            asset: AssetClass::Debt,
                            percent: 20.0,
                        },
                    ],
                },
                InvestmentRule {
                    goal: InvestmentGoal::Retirement,
                    min_percent: 20.0,
                    max_percent: 40.0,
                    priority: 8,
                    allocation: vec![
                        AssetAllocation {
                            asset: AssetClass::Equity,
                            percent: 70.0,
                        },
                        AssetAllocation {
                            asset: AssetClass::Debt,
                            percent: 30.0,
                        },
                    ],
                },
            ],
        }
    }

    pub fn growing_family_balanced() -> Self {
        Self {
            life_stage: LifeStage::GrowingFamily,
            risk_tolerance: RiskTolerance::Moderate,
            rules: vec![
                InvestmentRule {
                    goal: InvestmentGoal::ChildEducation,
                    min_percent: 15.0,
                    max_percent: 25.0,
                    priority: 10,
                    allocation: vec![
                        AssetAllocation {
                            asset: AssetClass::Hybrid,
                            percent: 60.0,
                        },
                        AssetAllocation {
                            asset: AssetClass::Debt,
                            percent: 40.0,
                        },
                    ],
                },
                InvestmentRule {
                    goal: InvestmentGoal::HomePurchase,
                    min_percent: 10.0,
                    max_percent: 20.0,
                    priority: 9,
                    allocation: vec![
                        AssetAllocation {
                            asset: AssetClass::Debt,
                            percent: 70.0,
                        },
                        AssetAllocation {
                            asset: AssetClass::Cash,
                            percent: 30.0,
                        },
                    ],
                },
                InvestmentRule {
                    goal: InvestmentGoal::Retirement,
                    min_percent: 25.0,
                    max_percent: 40.0,
                    priority: 8,
                    allocation: vec![
                        AssetAllocation {
                            asset: AssetClass::Equity,
                            percent: 60.0,
                        },
                        AssetAllocation {
                            asset: AssetClass::Debt,
                            percent: 40.0,
                        },
                    ],
                },
            ],
        }
    }

    pub fn retiree_income_focused() -> Self {
        Self {
            life_stage: LifeStage::Retiree,
            risk_tolerance: RiskTolerance::Low,
            rules: vec![
                InvestmentRule {
                    goal: InvestmentGoal::IncomeGeneration,
                    min_percent: 50.0,
                    max_percent: 70.0,
                    priority: 10,
                    allocation: vec![
                        AssetAllocation {
                            asset: AssetClass::Debt,
                            percent: 70.0,
                        },
                        AssetAllocation {
                            asset: AssetClass::RealAssets,
                            percent: 30.0,
                        },
                    ],
                },
                InvestmentRule {
                    goal: InvestmentGoal::HealthcareContingency,
                    min_percent: 10.0,
                    max_percent: 20.0,
                    priority: 9,
                    allocation: vec![AssetAllocation {
                        asset: AssetClass::Cash,
                        percent: 100.0,
                    }],
                },
                InvestmentRule {
                    goal: InvestmentGoal::LegacyPlanning,
                    min_percent: 5.0,
                    max_percent: 15.0,
                    priority: 7,
                    allocation: vec![
                        AssetAllocation {
                            asset: AssetClass::Equity,
                            percent: 50.0,
                        },
                        AssetAllocation {
                            asset: AssetClass::Debt,
                            percent: 50.0,
                        },
                    ],
                },
            ],
        }
    }
}

// Tax Side Profiles

impl TaxProfile {
    pub fn simple_income_tax(rate: f64) -> Self {
        Self {
            rules: vec![TaxRule {
                domain: TaxDomain::Income,
                rate_percent: rate,
                base: TaxBase::PercentageOfIncome,
                priority: 10,
                enabled: true,
            }],
        }
    }

    pub fn investment_tax(capital_gains_rate: f64) -> Self {
        Self {
            rules: vec![TaxRule {
                domain: TaxDomain::CapitalGains,
                rate_percent: capital_gains_rate,
                base: TaxBase::PercentageOfAmount,
                priority: 10,
                enabled: true,
            }],
        }
    }

    pub fn insurance_tax(rate: f64) -> Self {
        Self {
            rules: vec![TaxRule {
                domain: TaxDomain::Insurance,
                rate_percent: rate,
                base: TaxBase::PercentageOfAmount,
                priority: 5,
                enabled: true,
            }],
        }
    }

    pub fn custom(rules: Vec<TaxRule>) -> Self {
        Self { rules }
    }
}

// EMI Policy Default

impl EmiPolicy {
    pub fn salaried() -> Self {
        Self {
            max_emi_percent: 40.0,
            min_surplus_percent: 30.0,
            income_type: IncomeType::Salaried,
            joint_borrowers: false,
        }
    }

    pub fn self_employed() -> Self {
        Self {
            max_emi_percent: 30.0,
            min_surplus_percent: 40.0,
            income_type: IncomeType::SelfEmployed,
            joint_borrowers: false,
        }
    }

    pub fn high_income() -> Self {
        Self {
            max_emi_percent: 50.0,
            min_surplus_percent: 25.0,
            income_type: IncomeType::Salaried,
            joint_borrowers: false,
        }
    }

    pub fn low_income() -> Self {
        Self {
            max_emi_percent: 25.0,
            min_surplus_percent: 50.0,
            income_type: IncomeType::Variable,
            joint_borrowers: false,
        }
    }

    pub fn joint_borrowers() -> Self {
        Self {
            max_emi_percent: 45.0,
            min_surplus_percent: 30.0,
            income_type: IncomeType::Salaried,
            joint_borrowers: true,
        }
    }

    pub fn custom(
        max_emi_percent: f64,
        min_surplus_percent: f64,
        income_type: IncomeType,
        joint_borrowers: bool,
    ) -> Self {
        Self {
            max_emi_percent,
            min_surplus_percent,
            income_type,
            joint_borrowers,
        }
    }
}

// Loan Policy Default

impl LoanPolicy {
    pub fn salaried() -> Self {
        Self {
            emi_policy: EmiPolicy::salaried(),
            allow_business_loans: false,
            allow_personal_loans: true,
        }
    }

    pub fn self_employed() -> Self {
        Self {
            emi_policy: EmiPolicy::self_employed(),
            allow_business_loans: true,
            allow_personal_loans: true,
        }
    }

    pub fn high_income() -> Self {
        Self {
            emi_policy: EmiPolicy::high_income(),
            allow_business_loans: true,
            allow_personal_loans: true,
        }
    }

    pub fn low_income() -> Self {
        Self {
            emi_policy: EmiPolicy::low_income(),
            allow_business_loans: false,
            allow_personal_loans: false,
        }
    }

    pub fn joint_borrowers() -> Self {
        Self {
            emi_policy: EmiPolicy::joint_borrowers(),
            allow_business_loans: true,
            allow_personal_loans: true,
        }
    }

    pub fn custom(
        emi_policy: EmiPolicy,
        allow_business_loans: bool,
        allow_personal_loans: bool,
    ) -> Self {
        Self {
            emi_policy,
            allow_business_loans,
            allow_personal_loans,
        }
    }
}

/*
   Configuration of Math side parts like stats and  similarity calculations are in src/core/math/
*/

// Stats Side Profiles
impl StatProfile {
    /// Young professional, high-growth focused
    pub fn young_professional() -> Self {
        Self {
            metrics: vec![
                // Health
                StatMetric {
                    name: "BMI".into(),
                    category: StatCategory::Health,
                    value: 0.0,
                    target: Some(22.0),
                    measurement: MeasurementType::Float,
                    weight: 0.2,
                    history: vec![],
                },
                StatMetric {
                    name: "Resting Heart Rate".into(),
                    category: StatCategory::Health,
                    value: 0.0,
                    target: Some(70.0),
                    measurement: MeasurementType::Integer,
                    weight: 0.1,
                    history: vec![],
                },
                StatMetric {
                    name: "Sleep Hours".into(),
                    category: StatCategory::Health,
                    value: 0.0,
                    target: Some(8.0),
                    measurement: MeasurementType::Float,
                    weight: 0.1,
                    history: vec![],
                },
                // Finance
                StatMetric {
                    name: "Net Worth".into(),
                    category: StatCategory::Finance,
                    value: 0.0,
                    target: Some(50_000.0),
                    measurement: MeasurementType::Float,
                    weight: 0.3,
                    history: vec![],
                },
                StatMetric {
                    name: "Emergency Fund".into(),
                    category: StatCategory::Finance,
                    value: 0.0,
                    target: Some(15_000.0),
                    measurement: MeasurementType::Float,
                    weight: 0.2,
                    history: vec![],
                },
                // Productivity
                StatMetric {
                    name: "Focus Hours".into(),
                    category: StatCategory::Productivity,
                    value: 0.0,
                    target: Some(6.0),
                    measurement: MeasurementType::Float,
                    weight: 0.1,
                    history: vec![],
                },
            ],
            alert_policy: AlertPolicy::standard(),
        }
    }

    /// Family with dependents, balanced focus
    pub fn family_with_dependents() -> Self {
        Self {
            metrics: vec![
                // Health
                StatMetric {
                    name: "BMI".into(),
                    category: StatCategory::Health,
                    value: 0.0,
                    target: Some(24.0),
                    measurement: MeasurementType::Float,
                    weight: 0.15,
                    history: vec![],
                },
                // Finance
                StatMetric {
                    name: "Debt-to-Income Ratio".into(),
                    category: StatCategory::Finance,
                    value: 0.0,
                    target: Some(35.0),
                    measurement: MeasurementType::Percentage,
                    weight: 0.25,
                    history: vec![],
                },
                StatMetric {
                    name: "Retirement Savings".into(),
                    category: StatCategory::Finance,
                    value: 0.0,
                    target: Some(100_000.0),
                    measurement: MeasurementType::Float,
                    weight: 0.2,
                    history: vec![],
                },
                StatMetric {
                    name: "Childcare Hours".into(),
                    category: StatCategory::Lifestyle,
                    value: 0.0,
                    target: Some(40.0), // average weekly childcare target
                    measurement: MeasurementType::Float,
                    weight: 0.1,
                    history: vec![],
                },
                // Productivity
                StatMetric {
                    name: "Focus Hours".into(),
                    category: StatCategory::Productivity,
                    value: 0.0,
                    target: Some(5.0),
                    measurement: MeasurementType::Float,
                    weight: 0.1,
                    history: vec![],
                },
            ],
            alert_policy: AlertPolicy::standard(),
        }
    }

    /// Retiree, income-focused
    pub fn retiree_income_focused() -> Self {
        Self {
            metrics: vec![
                // Health
                StatMetric {
                    name: "BMI".into(),
                    category: StatCategory::Health,
                    value: 0.0,
                    target: Some(23.0),
                    measurement: MeasurementType::Float,
                    weight: 0.1,
                    history: vec![],
                },
                // Finance
                StatMetric {
                    name: "Income Generation".into(),
                    category: StatCategory::Finance,
                    value: 0.0,
                    target: Some(60_000.0),
                    measurement: MeasurementType::Float,
                    weight: 0.4,
                    history: vec![],
                },
                StatMetric {
                    name: "Healthcare Contingency".into(),
                    category: StatCategory::Finance,
                    value: 0.0,
                    target: Some(15_000.0),
                    measurement: MeasurementType::Float,
                    weight: 0.2,
                    history: vec![],
                },
                StatMetric {
                    name: "Legacy Planning".into(),
                    category: StatCategory::Finance,
                    value: 0.0,
                    target: Some(50_000.0),
                    measurement: MeasurementType::Float,
                    weight: 0.1,
                    history: vec![],
                },
                // Lifestyle
                StatMetric {
                    name: "Leisure Hours".into(),
                    category: StatCategory::Lifestyle,
                    value: 0.0,
                    target: Some(15.0), // weekly hours
                    measurement: MeasurementType::Float,
                    weight: 0.1,
                    history: vec![],
                },
            ],
            alert_policy: AlertPolicy::relaxed(),
        }
    }

    /// Single parent, moderate-risk, highly tracked
    pub fn single_parent_profile() -> Self {
        Self {
            metrics: vec![
                // Health
                StatMetric {
                    name: "BMI".into(),
                    category: StatCategory::Health,
                    value: 0.0,
                    target: Some(23.0),
                    measurement: MeasurementType::Float,
                    weight: 0.15,
                    history: vec![],
                },
                StatMetric {
                    name: "Resting Heart Rate".into(),
                    category: StatCategory::Health,
                    value: 0.0,
                    target: Some(72.0),
                    measurement: MeasurementType::Integer,
                    weight: 0.1,
                    history: vec![],
                },
                // Finance
                StatMetric {
                    name: "Debt-to-Income Ratio".into(),
                    category: StatCategory::Finance,
                    value: 0.0,
                    target: Some(30.0),
                    measurement: MeasurementType::Percentage,
                    weight: 0.25,
                    history: vec![],
                },
                StatMetric {
                    name: "Emergency Fund".into(),
                    category: StatCategory::Finance,
                    value: 0.0,
                    target: Some(20_000.0),
                    measurement: MeasurementType::Float,
                    weight: 0.2,
                    history: vec![],
                },
                StatMetric {
                    name: "Childcare Hours".into(),
                    category: StatCategory::Lifestyle,
                    value: 0.0,
                    target: Some(50.0), // weekly hours covered
                    measurement: MeasurementType::Float,
                    weight: 0.1,
                    history: vec![],
                },
                // Productivity
                StatMetric {
                    name: "Focus Hours".into(),
                    category: StatCategory::Productivity,
                    value: 0.0,
                    target: Some(4.0),
                    measurement: MeasurementType::Float,
                    weight: 0.1,
                    history: vec![],
                },
            ],
            alert_policy: AlertPolicy::strict(),
        }
    }
    pub fn generate_alerts(&self) -> Vec<StatAlert> {
        crate::core::math::stats::generate_alerts(self)
    }
}

// Alert Policy Default

impl AlertPolicy {
    /// Default – conservative, general-purpose
    pub fn standard() -> Self {
        Self {
            target_warning_percent: 10.0,
            target_critical_percent: 20.0,
            trend_warning_percent: 15.0,
        }
    }

    /// More aggressive monitoring (e.g., single parent, health-focused)
    pub fn strict() -> Self {
        Self {
            target_warning_percent: 5.0,
            target_critical_percent: 15.0,
            trend_warning_percent: 10.0,
        }
    }

    /// Relaxed monitoring (e.g., retiree)
    pub fn relaxed() -> Self {
        Self {
            target_warning_percent: 15.0,
            target_critical_percent: 30.0,
            trend_warning_percent: 20.0,
        }
    }

    /// Full customization (mirror EMI/Loan)
    pub fn custom(
        target_warning_percent: f64,
        target_critical_percent: f64,
        trend_warning_percent: f64,
    ) -> Self {
        Self {
            target_warning_percent,
            target_critical_percent,
            trend_warning_percent,
        }
    }
}

/*
    Utilities related to configuration 
*/

impl fmt::Display for AppError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AppError::InvalidInput(msg) => write!(f, "Invalid input: {}", msg),
            AppError::CalculationError(msg) => write!(f, "Calculation error: {}", msg),
            AppError::ProfileNotFound(msg) => write!(f, "Profile not found: {}", msg),
            AppError::AllocationError(msg) => write!(f, "Allocation error: {}", msg),
            AppError::ExternalServiceError(msg) => write!(f, "External service error: {}", msg),
            AppError::Other(msg) => write!(f, "Other error: {}", msg),
        }
    }
}

impl Error for AppError {}