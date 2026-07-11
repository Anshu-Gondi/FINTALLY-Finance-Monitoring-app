use serde::{Deserialize, Serialize};
use chrono::{DateTime, Utc};

// ──────────────────────────────────────────────
// Enums
// ──────────────────────────────────────────────

#[derive(Debug, Serialize, Deserialize, sqlx::Type, Clone, Copy)]
#[sqlx(type_name = "recurring_frequency")]
pub enum RecurringFrequency {
    Daily,
    Weekly,
    Monthly,
}

#[derive(Debug, Serialize, Deserialize, sqlx::Type, Clone, Copy)]
#[sqlx(type_name = "warning_code")]
pub enum WarningCode {
    #[serde(rename = "LIMIT_EXCEEDED")]
    LimitExceeded,
}

// ──────────────────────────────────────────────
// Auth
// ──────────────────────────────────────────────

#[derive(Debug, Serialize, Deserialize)]
pub struct SignupRequest {
    pub name: String,
    pub email: String,
    pub password: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct LoginRequest {
    pub email: String,
    pub password: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct GoogleAuthRequest {
    pub token: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct AuthResponse {
    pub success: bool,
    pub token: Option<String>,
    pub message: Option<String>,
}

// ──────────────────────────────────────────────
// Transactions
// ──────────────────────────────────────────────

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionCreate {
    pub name: String,
    pub price: f64,
    pub description: String,
    pub datetime: DateTime<Utc>,
    #[serde(default = "default_category")]
    pub category: String,
    #[serde(rename = "isRecurring", default)]
    pub is_recurring: bool,
    #[serde(rename = "recurringFrequency")]
    pub recurring_frequency: Option<RecurringFrequency>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionUpdate {
    pub name: Option<String>,
    pub price: Option<f64>,
    pub description: Option<String>,
    pub datetime: Option<DateTime<Utc>>,
    pub category: Option<String>,
    #[serde(rename = "isRecurring")]
    pub is_recurring: Option<bool>,
    #[serde(rename = "recurringFrequency")]
    pub recurring_frequency: Option<RecurringFrequency>,
}

fn default_category() -> String {
    "General".to_string()
}

// ──────────────────────────────────────────────
// Budget
// ──────────────────────────────────────────────

#[derive(Debug, Serialize, Deserialize)]
pub struct BudgetCreate {
    pub amount: f64,
    #[serde(default = "default_budget_category")]
    pub category: String,
    #[serde(rename = "startDate")]
    pub start_date: Option<DateTime<Utc>>,
    #[serde(rename = "endDate")]
    pub end_date: Option<DateTime<Utc>>,
    #[serde(rename = "isRecurring", default)]
    pub is_recurring: bool,
}

fn default_budget_category() -> String {
    "Overall".to_string()
}

// ──────────────────────────────────────────────
// EMI
// ──────────────────────────────────────────────

#[derive(Debug, Serialize, Deserialize)]
pub struct EmiCalculateRequest {
    pub principal: f64,
    #[serde(rename = "annualRate")]
    pub annual_rate: f64,
    pub months: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct EmiCheckRequest {
    pub principal: f64,
    #[serde(rename = "annualRate")]
    pub annual_rate: f64,
    pub months: i32,
    #[serde(default = "default_budget_category")]
    pub category: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct EmiCreateRequest {
    pub principal: f64,
    #[serde(rename = "annualRate")]
    pub annual_rate: f64,
    pub months: i32,
    #[serde(default = "default_emi_category")]
    pub category: String,
    #[serde(default = "default_emi_name")]
    pub name: String,
}

fn default_emi_category() -> String { "EMI".to_string() }
fn default_emi_name() -> String { "Loan EMI".to_string() }

// ──────────────────────────────────────────────
// Feedback
// ──────────────────────────────────────────────

#[derive(Debug, Serialize, Deserialize)]
pub struct FeedbackCreate {
    pub name: String,
    pub email: String,
    pub message: String,
}

// ──────────────────────────────────────────────
// Financial Analytics Domain Objects
// ──────────────────────────────────────────────

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionAnomaly {
    pub datetime: String,
    pub price: f64,
    pub zscore: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionAnomalyResult {
    pub threshold: f64,
    pub anomalies: Vec<TransactionAnomaly>,
    pub count: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct CategoryDriftPoint {
    pub category: String,
    pub percent_change: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct CategoryDriftResult {
    pub category_drift: Vec<CategoryDriftPoint>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct RecurringImpactResult {
    pub monthly_recurring_cost: f64,
    pub yearly_projection: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct BudgetUtilizationResult {
    pub budget_amount: f64,
    pub spent: f64,
    pub remaining: f64,
    pub usage_percent: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct BurnRateResult {
    pub daily_burn_rate: f64,
    pub days_until_exhaustion: i32,
    pub days_elapsed: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct AnalyticsPoint {
    pub period: String,
    pub income: f64,
    pub expense: f64,
    pub total: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct CategoryPoint {
    pub category: String,
    pub total: f64,
    pub count: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct AnalyticsMeta {
    pub truncated: bool,
    pub limit_applied: bool,
    pub row_count: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Warning {
    pub code: WarningCode,
    pub message: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct AnalyticsResult {
    pub data: Vec<AnalyticsPoint>,
    pub meta: Option<AnalyticsMeta>,
    pub warnings: Option<Vec<Warning>>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct CategoryResult {
    pub data: Vec<CategoryPoint>,
    pub meta: Option<AnalyticsMeta>,
    pub warnings: Option<Vec<Warning>>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct BudgetBreachResult {
    pub breach_probability: f64,
    pub expected_spend: f64,
    #[serde(rename = "p50_days_to_breach")]
    pub p50_days_to_breach: Option<i32>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct EmiPressureResult {
    pub monthly_emi: f64,
    pub emi_ratio: f64,
    pub survivability_score: f64,
    pub risk_level: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct CashflowForecastPoint {
    pub horizon_days: i32,
    pub expected_balance: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct CashflowForecastResult {
    pub points: Vec<CashflowForecastPoint>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct RecurringAnomaly {
    pub description: String,
    pub severity: f64,
    pub deviation_percent: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct IncomeStabilityResult {
    pub income_volatility: f64,
    pub salary_predictability_score: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct SavingsOptimizationResult {
    pub saving_rate_percent: f64,
    pub financial_health_score: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct NetWorthResult {
    pub total_assets: f64,
    pub total_liabilities: f64,
    pub net_worth: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct FinancialHealthScoreResult {
    pub score: f64,
    pub savings_rate: f64,
    pub income_stability: f64,
    pub burn_rate: f64,
    pub risk_level: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct SpendingPatternPoint {
    pub category: String,
    pub percent: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct SpendingPatternResult {
    pub patterns: Vec<SpendingPatternPoint>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct GoalProjectionResult {
    pub current_savings: f64,
    pub monthly_savings: f64,
    pub target_amount: f64,
    pub months_to_goal: f64,
}