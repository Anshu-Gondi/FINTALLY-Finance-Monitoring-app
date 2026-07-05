use axum::{
    extract::{ Query, State },
    response::IntoResponse,
    Json,
};
use chrono::{ DateTime, Utc };
use serde::{ Deserialize, Serialize };
use sqlx::PgPool;
use uuid::Uuid;

// Import your Claims middleware extractor
use crate::auth::Claims;
// Import your service methods
use analytics_engine::analytics_service::{
    daily_summary,
    period_summary,
    lifetime_analysis,
    min_max_transaction,
    category_summary,
    trend_summary,
    emi_pressure,
    cashflow_forecast,
    budget_breach_prediction,
    recurring_anomalies,
    transaction_anomalies,
    category_drift_analysis,
    recurring_impact_analysis,
    budget_utilization_analysis,
    burn_rate_analysis,
    income_stability_analysis,
    savings_optimization_analysis,
    net_worth_analysis_service,
    financial_health_score,
    spending_patterns,
    goal_projection,
};

// --- QUERY PARAMETER STRUCTURES ---

#[derive(Deserialize)]
pub struct DailyQuery {
    interval: u32,
}

#[derive(Deserialize)]
pub struct PeriodQuery {
    range: String,
    bucket_days: Option<u32>,
}

#[derive(Deserialize)]
pub struct CategoryQuery {
    start: Option<DateTime<Utc>>,
    end: Option<DateTime<Utc>>,
    tx_type: Option<String>,
    keyword: Option<String>,
    limit: Option<usize>,
}

#[derive(Deserialize)]
pub struct TrendQuery {
    range: String,
}

#[derive(Deserialize)]
pub struct CashflowQuery {
    // Expects comma-separated list or multiple parameters: e.g., ?horizons=30&horizons=60
    horizons: Vec<i32>,
}

#[derive(Deserialize)]
pub struct BudgetBreachQuery {
    end_date: DateTime<Utc>,
    simulations: Option<usize>,
}

#[derive(Deserialize)]
pub struct AnomalyQuery {
    threshold: Option<f64>,
}

#[derive(Deserialize)]
pub struct GoalQuery {
    target_amount: f64,
}

// --- API ERROR WRAPPER FOR ROUTING ---

#[derive(Serialize)]
pub struct ApiErrorResponse {
    error: String,
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
                "The User ID payload cannot be parsed into a valid UUID format.".to_string(), // Convert &str to String
            ),
            ApiError::DatabaseError(err) => (
                axum::http::StatusCode::INTERNAL_SERVER_ERROR, 
                err // This is already a String!
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
    Query(query): Query<DailyQuery>
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = daily_summary(&pool, user_id, query.interval).await.map_err(|e|
        ApiError::DatabaseError(e.to_string())
    )?;

    Ok(Json(result))
}

pub async fn handle_period_summary(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<PeriodQuery>
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = period_summary(&pool, user_id, &query.range, query.bucket_days).await.map_err(|e|
        ApiError::DatabaseError(e.to_string())
    )?;

    Ok(Json(result))
}

pub async fn handle_lifetime_analysis(
    State(pool): State<PgPool>,
    claims: Claims
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = lifetime_analysis(&pool, user_id).await.map_err(|e|
        ApiError::DatabaseError(e.to_string())
    )?;

    Ok(Json(result))
}

pub async fn handle_min_max_transaction(
    State(pool): State<PgPool>,
    claims: Claims
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let (min_tx, max_tx) = min_max_transaction(&pool, user_id).await.map_err(|e|
        ApiError::DatabaseError(e.to_string())
    )?;

    // Return as an explicit structure or tuple mapping
    Ok(Json((min_tx, max_tx)))
}

pub async fn handle_category_summary(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<CategoryQuery>
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = category_summary(
        &pool,
        user_id,
        query.start,
        query.end,
        query.tx_type.as_deref(),
        query.keyword.as_deref(),
        query.limit
    ).await.map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_trend_summary(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<TrendQuery>
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = trend_summary(&pool, user_id, &query.range).await.map_err(|e|
        ApiError::DatabaseError(e.to_string())
    )?;

    Ok(Json(result))
}

pub async fn handle_emi_pressure(
    State(pool): State<PgPool>,
    claims: Claims
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = emi_pressure(&pool, user_id).await.map_err(|e|
        ApiError::DatabaseError(e.to_string())
    )?;

    Ok(Json(result))
}

pub async fn handle_cashflow_forecast(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<CashflowQuery>
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = cashflow_forecast(&pool, user_id, query.horizons).await.map_err(|e|
        ApiError::DatabaseError(e.to_string())
    )?;

    Ok(Json(result))
}

pub async fn handle_budget_breach_prediction(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<BudgetBreachQuery>
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;

    // Default to 1000 simulations if not specified in query params
    let simulations = query.simulations.unwrap_or(1000);

    let result = budget_breach_prediction(
        &pool,
        user_id,
        query.end_date,
        simulations
    ).await.map_err(|e| ApiError::DatabaseError(e.to_string()))?;

    Ok(Json(result))
}

pub async fn handle_recurring_anomalies(
    State(pool): State<PgPool>,
    claims: Claims
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;
    let result = recurring_anomalies(&pool, user_id).await.map_err(|e|
        ApiError::DatabaseError(e.to_string())
    )?;

    Ok(Json(result))
}

pub async fn handle_transaction_anomalies(
    State(pool): State<PgPool>,
    claims: Claims,
    Query(query): Query<AnomalyQuery>
) -> Result<impl IntoResponse, ApiError> {
    let user_id = parse_user_id(&claims)?;

    // Default standard MAD threshold z-score to 3.0 if none provided
    let threshold = query.threshold.unwrap_or(3.0);

    let result = transaction_anomalies(&pool, user_id, threshold).await.map_err(|e|
        ApiError::DatabaseError(e.to_string())
    )?;

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