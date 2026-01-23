// src/core/types.rs

/* Types and Enums for Financial Planning 
    includes Files: savings.rs, budgeting.rs, cashflow.rs, investments.rs, emi.rs, loans.rs, tax.rs
    Includes:
    - Savings and Finance Related Types and Enums
    - Budgeting Related Types and Enums
    - Cashflow Related Types and Enums
    - Investment Related Types and Enums
    - Taxation Related Types and Enums
*/

// Savings and Finance Related Types and Enums

#[derive(Debug, Clone)]
pub struct EmergencyFundPolicy {
    pub months: f64,
    pub expense_multiplier: f64,
}

#[derive(Debug, Clone)]
pub struct SavingsPolicy {
    pub monthly_contribution: f64,
    pub annual_growth_rate: f64,
}

#[derive(Debug, Clone)]
pub struct FinanceProfile {
    pub emergency_fund: EmergencyFundPolicy,
    pub savings: SavingsPolicy,
}

// Budgeting Related Types and Enums

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum BudgetCategory {
    Housing,
    Utilities,
    Food,
    Transportation,
    Healthcare,
    Insurance,
    Childcare,
    Education,
    DebtRepayment,
    Savings,
    EmergencyFund,
    Investments,
    Lifestyle,
    Miscellaneous,
}

#[derive(Debug, Clone)]
pub struct BudgetRule {
    pub category: BudgetCategory,
    pub min_percent: f64, // minimum % of income
    pub max_percent: f64, // cap
    pub priority: u8,     // higher = funded first
}

#[derive(Debug, Clone)]
pub struct BudgetProfile {
    pub rules: Vec<BudgetRule>,
}

// Cashflow Related Types and Enums

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum CashflowBucket {
    Essentials,
    FinancialStability,
    Lifestyle,
}

#[derive(Debug, Clone)]
pub struct CashflowRule {
    pub bucket: CashflowBucket,
    pub min_percent: f64,
    pub max_percent: f64,
    pub priority: u8,
}

#[derive(Debug, Clone)]
pub enum CashflowMode {
    FixedRatio,
    PriorityBased,
}

#[derive(Debug, Clone)]
pub struct CashflowProfile {
    pub rules: Vec<CashflowRule>,
    pub mode: CashflowMode,
}

// Investment Related Types and Enums

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum LifeStage {
    YoungProfessional,
    GrowingFamily,
    MatureSaver,
    Retiree,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum RiskTolerance {
    Low,
    Moderate,
    High,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum InvestmentGoal {
    EmergencyBuffer,
    Retirement,
    ChildEducation,
    HomePurchase,
    WealthGrowth,
    IncomeGeneration,
    HealthcareContingency,
    LegacyPlanning,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum AssetClass {
    Equity,
    Debt,
    Hybrid,
    RealAssets,
    Cash,
}

#[derive(Debug, Clone)]
pub struct AssetAllocation {
    pub asset: AssetClass,
    pub percent: f64,
}

#[derive(Debug, Clone)]
pub struct InvestmentRule {
    pub goal: InvestmentGoal,
    pub min_percent: f64,
    pub max_percent: f64,
    pub priority: u8,
    pub allocation: Vec<AssetAllocation>,
}

#[derive(Debug, Clone)]
pub struct InvestmentProfile {
    pub life_stage: LifeStage,
    pub risk_tolerance: RiskTolerance,
    pub rules: Vec<InvestmentRule>,
}

// Taxation Related Types and Enums

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum TaxDomain {
    Income,
    CapitalGains,
    Insurance,
    Healthcare,
    Funeral,
    Custom(String), // user-defined
}

#[derive(Debug, Clone)]
pub enum TaxBase {
    FlatAmount(f64),
    PercentageOfIncome,
    PercentageOfAmount,
}

#[derive(Debug, Clone)]
pub struct TaxRule {
    pub domain: TaxDomain,
    pub rate_percent: f64,   // user supplied
    pub base: TaxBase,
    pub priority: u8,        // order of application
    pub enabled: bool,
}

#[derive(Debug, Clone)]
pub struct TaxProfile {
    pub rules: Vec<TaxRule>,
}

// Emi and Loans Related Types and Enums

#[derive(Debug, Clone)]
pub enum IncomeType {
    Salaried,
    SelfEmployed,
    Variable,
}

#[derive(Debug, Clone)]
pub struct EmiPolicy {
    pub max_emi_percent: f64,   // % of monthly income
    pub min_surplus_percent: f64, // income left after EMI
    pub income_type: IncomeType,
    pub joint_borrowers: bool,
}

pub struct LoanPolicy {
    pub emi_policy: EmiPolicy,
    pub allow_business_loans: bool,
    pub allow_personal_loans: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub enum LoanPurpose {
    Personal,
    Business,
    Home,
    Education,
}

#[derive(Debug, Clone)]
pub struct LoanRequest {
    pub monthly_income: f64,
    pub existing_emi: f64,
    pub requested_emi: f64,
    pub credit_score: u16,
    pub purpose: LoanPurpose,
    pub is_joint: bool,
}

#[derive(Debug, Clone)]
pub struct LoanAssessment {
    pub approved: bool,
    pub max_allowed_emi: f64,
    pub risk_score: f64,
    pub reason: String,
}

// Stats, Alerts and Measurements Related Types and Enums

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum StatCategory {
    Health,
    Finance,
    Productivity,
    Lifestyle,
}

#[derive(Debug, Clone)]
pub enum MeasurementType {
    Integer,
    Float,
    Percentage,
    Score, // e.g., 300-850 credit score
}

#[derive(Debug, Clone)]
pub struct StatMetric {
    pub name: String,               // e.g., "BMI", "Net Worth", "Sleep Quality"
    pub category: StatCategory,
    pub value: f64,
    pub target: Option<f64>,        // optional ideal value
    pub measurement: MeasurementType,
    pub weight: f64,                // relative importance (0-1)
    pub history: Vec<f64>, // store past metrics for trend analysis
}

#[derive(Debug, Clone)]
pub struct StatProfile {
    pub metrics: Vec<StatMetric>,
    pub alert_policy: AlertPolicy,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AlertLevel {
    Info,
    Warning,
    Critical,
}

#[derive(Debug, Clone)]
pub struct StatAlert {
    pub metric_name: String,
    pub category: StatCategory,
    pub message: String,
    pub level: AlertLevel,
}

#[derive(Debug, Clone)]
pub struct AlertPolicy {
    pub target_warning_percent: f64,
    pub target_critical_percent: f64,
    pub trend_warning_percent: f64,
}

// Similarity Calculation Related Types and Enums

#[derive(Debug, Clone)]
pub enum SimilarityMetric {
    Euclidean,
    Cosine,
    Pearson,
}

#[derive(Debug, Clone)]
pub struct UserProfileVector {
    pub user_id: String,
    pub metrics: Vec<f64>, // flattened vector of metrics
}

/*
    Utils Types and Enums
    includes Files: logging.rs, errors.rs
*/

// Core error type for the application
#[derive(Debug)]
pub enum AppError {
    InvalidInput(String),
    CalculationError(String),
    ProfileNotFound(String),
    AllocationError(String),
    ExternalServiceError(String), // placeholder if you ever integrate APIs
    Other(String),
}

// Logging Related Types and Enums
#[derive(Debug)]
pub enum LogLevel {
    Info,
    Warn,
    Error,
}