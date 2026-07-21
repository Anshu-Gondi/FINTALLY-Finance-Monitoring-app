use axum::{
    extract::{Query, RawQuery, State},
    response::IntoResponse,
    Json,
};
use chrono::{DateTime, NaiveDate, Utc};
use serde::{de, Deserialize, Deserializer, Serialize};
use sqlx::PgPool;
use uuid::Uuid;

// Import your Claims middleware extractor
use crate::auth::Claims;
// Import your service methods
use analytics_engine::analytics_service::{
    budget_breach_prediction, budget_utilization_analysis, burn_rate_analysis,
    cashflow_forecast, category_drift_analysis, category_summary, daily_summary,
    emi_pressure, financial_health_score, goal_projection, income_stability_analysis,
    lifetime_analysis, min_max_transaction, net_worth_analysis_service, period_summary,
    recurring_anomalies, recurring_impact_analysis, savings_optimization_analysis,
    spending_patterns, transaction_anomalies, trend_summary,
};

// --- CUSTOM DESERIALIZERS FOR QUERY PARAMS ---

/// Helper to parse flexible date formats: "YYYY-MM-DD" or full RFC3339 / ISO-8601 strings.
fn parse_flexible_date<'de, D>(deserializer: D) -> Result<DateTime<Utc>, D::Error>
where
    D: Deserializer<'de>,
{
    let s = String::deserialize(deserializer)?;

    // 1. Try parsing full RFC3339 / ISO 8601 format (e.g. "2026-08-19T00:00:00Z")
    if let Ok(dt) = DateTime::parse_from_rfc3339(&s) {
        return Ok(dt.with_timezone(&Utc));
    }

    // 2. Fallback to simple date format ("YYYY-MM-DD")
    if let Ok(date) = NaiveDate::parse_from_str(&s, "%Y-%m-%d") {
        if let Some(naive_dt) = date.and_hms_opt(23, 59, 59) {
            return Ok(DateTime::<Utc>::from_naive_utc_and_offset(naive_dt, Utc));
        }
    }

    Err(de::Error::custom(format!(
        "Invalid date format for '{s}'. Expected 'YYYY-MM-DD' or RFC3339 string."
    )))
}

/// Helper to parse flexible query parameters for `horizons`:
/// - Multiple params: `?horizons=30&horizons=60`
/// - Comma-separated: `?horizons=30,60,90`
/// - Single param: `?horizons=30`
fn parse_flexible_horizons<'de, D>(deserializer: D) -> Result<Vec<i32>, D::Error>
where
    D: Deserializer<'de>,
{
    #[derive(Deserialize)]
    #[serde(untagged)]
    enum MultiFormat {
        Vec(Vec<i32>),
        Single(i32),
        Str(String),
    }

    match Option::<MultiFormat>::deserialize(deserializer)? {
        Some(MultiFormat::Vec(v)) => Ok(v),
        Some(MultiFormat::Single(i)) => Ok(vec![i]),
        Some(MultiFormat::Str(s)) => {
            let parsed: Result<Vec<i32>, _> = s
                .split(',')
                .map(|item| item.trim().parse::<i32>())
                .collect();
            parsed.map_err(de::Error::custom)
        }
        None => Ok(vec![30, 60, 90]), // Safe default fallback
    }
}

// --- QUERY PARAMETER STRUCTURES ---

#[derive(Deserialize)]
pub struct DailyQuery {
    pub interval: u32,
}

#[derive(Deserialize)]
pub struct PeriodQuery {
    pub range: String,
    pub bucket_days: Option<u32>,
}

#[derive(Deserialize)]
pub struct CategoryQuery {
    pub start: Option<DateTime<Utc>>,
    pub end: Option<DateTime<Utc>>,
    pub tx_type: Option<String>,
    pub keyword: Option<String>,
    pub limit: Option<usize>,
}

#[derive(Deserialize)]
pub struct TrendQuery {
    pub range: String,
}

#[derive(Deserialize)]
pub struct CashflowQuery {
    #[serde(default, deserialize_with = "parse_flexible_horizons")]
    pub horizons: Vec<i32>,
}

#[derive(Deserialize)]
pub struct BudgetBreachQuery {
    #[serde(deserialize_with = "parse_flexible_date")]
    pub end_date: DateTime<Utc>,
    pub simulations: Option<usize>,
}

#[derive(Deserialize)]
pub struct AnomalyQuery {
    pub threshold: Option<f64>,
}

#[derive(Deserialize)]
pub struct GoalQuery {
    pub target_amount: f64,
}

// --- API ERROR WRAPPER FOR ROUTING ---

#[derive(Serialize)]
pub struct ApiErrorResponse {
    pub error: String,
}

pub enum ApiError {
    InvalidUserId,
    DatabaseError(String),
}

impl IntoResponse for ApiError {
    fn into_response(self) -> axum::response::Response {
        let (status, msg): (axum::http::StatusCode, String) = match self {
            ApiError::InvalidUserId => (
                axum::http::StatusCode::BAD_REQUEST,
                "The User ID payload cannot be parsed into a valid UUID format.".to_string(),
            ),
            ApiError::DatabaseError(err) => (
                axum::http::StatusCode::INTERNAL_SERVER_ERROR,
                err,
            ),
        };

        (status, axum::Json(serde_json::json!({ "error": msg }))).into_response()
    }
}

// Helper to convert internal claims string to Uuid efficiently
fn parse_user_id(claims: &Claims) -> Result<Uuid, ApiError> {
    Uuid::parse_str(&claims.user_id).map_err(|_| ApiError::InvalidUserId)
}

// --- HANDLERS ---

pub async fn handle_daily_summary(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<DailyQuery>,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = daily_summary(&pool, user_id, query.interval)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_period_summary(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<PeriodQuery>,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = period_summary(&pool, user_id, &query.range, query.bucket_days)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_lifetime_analysis(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = lifetime_analysis(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_min_max_transaction(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = min_max_transaction(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_category_summary(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<CategoryQuery>,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = category_summary(
        &pool,
        user_id,
        query.start,
        query.end,
        query.tx_type.as_deref(),
        query.keyword.as_deref(),
        query.limit,
    )
    .await
    .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_trend_summary(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<TrendQuery>,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = trend_summary(&pool, user_id, &query.range)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_emi_pressure(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = emi_pressure(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_cashflow_forecast(
    State(pool): State<PgPool>,
    claims: Claims,
    RawQuery(raw_query): RawQuery,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;

    // Manually parse query parameters to handle repeated keys or comma-separated values
    let horizons: Vec<i32> = raw_query
        .as_deref()
        .unwrap_or("")
        .split('&')
        .filter_map(|pair| {
            let mut parts = pair.split('=');
            if parts.next() == Some("horizons") {
                parts.next().and_then(|val| val.parse::<i32>().ok())
            } else {
                None
            }
        })
        .collect();

    let final_horizons = if horizons.is_empty() {
        vec![30, 60, 90]
    } else {
        horizons
    };

    let result = cashflow_forecast(&pool, user_id, final_horizons)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_budget_breach_prediction(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<BudgetBreachQuery>,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;

    // Default to 1000 simulations if not specified in query params
    let simulations = query.simulations.unwrap_or(1000);

    let result = budget_breach_prediction(&pool, user_id, query.end_date, simulations)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_recurring_anomalies(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = recurring_anomalies(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_transaction_anomalies(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<AnomalyQuery>,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;

    // Default standard MAD threshold z-score to 3.0 if none provided
    let threshold = query.threshold.unwrap_or(3.0);

    let result = transaction_anomalies(&pool, user_id, threshold)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_category_drift(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = category_drift_analysis(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_recurring_impact(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = recurring_impact_analysis(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_budget_utilization(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = budget_utilization_analysis(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_burn_rate(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = burn_rate_analysis(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_income_stability(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = income_stability_analysis(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_savings_optimization(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = savings_optimization_analysis(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_net_worth(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = net_worth_analysis_service(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_financial_health_score(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = financial_health_score(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_spending_patterns(
    State(pool): State<PgPool>,
    claims: Claims,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = spending_patterns(&pool, user_id)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_goal_projection(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<GoalQuery>,
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = goal_projection(&pool, user_id, query.target_amount)
        .await
        .map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}
