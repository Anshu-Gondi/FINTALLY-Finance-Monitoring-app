use fintally_db::DbContext;
use axum::{ routing::{ get, post, delete }, Router, response::IntoResponse, Json, Extension };

pub mod auth;
pub mod routes; // Declare the routes directory module

use crate::auth::Claims;

async fn get_protected_finance_summary(claims: Claims) -> impl IntoResponse {
    println!("🔐 Authenticated Request processed for User ID: {}", claims.user_id);
    Json(
        serde_json::json!({
        "status": "success",
        "data": { "balance": 1250.45, "currency": "INR" }
    })
    )
}

/// Helper function to create an isolated, State-managed analytics sub-router tree
fn build_analytics_router(pool: sqlx::PgPool) -> Router {
    Router::new()
        // --- Core Summaries & Timelines ---
        .route("/daily", get(routes::analytics::handle_daily_summary))
        .route("/period", get(routes::analytics::handle_period_summary))
        .route("/lifetime", get(routes::analytics::handle_lifetime_analysis))
        .route("/min-max", get(routes::analytics::handle_min_max_transaction))
        .route("/category", get(routes::analytics::handle_category_summary))
        .route("/trend", get(routes::analytics::handle_trend_summary))

        // --- Loans, Cashflow & Forecast Simulations ---
        .route("/emi-pressure", get(routes::analytics::handle_emi_pressure))
        .route("/cashflow-forecast", get(routes::analytics::handle_cashflow_forecast))
        .route("/budget-breach", get(routes::analytics::handle_budget_breach_prediction))

        // --- Anomaly Detections ---
        .route("/anomalies/recurring", get(routes::analytics::handle_recurring_anomalies))
        .route("/anomalies/transactions", get(routes::analytics::handle_transaction_anomalies))

        // --- Budgeting, Drift & Stability Analysis ---
        .route("/drift", get(routes::analytics::handle_category_drift))
        .route("/recurring-impact", get(routes::analytics::handle_recurring_impact))
        .route("/utilization", get(routes::analytics::handle_budget_utilization))
        .route("/burn-rate", get(routes::analytics::handle_burn_rate))
        .route("/income-stability", get(routes::analytics::handle_income_stability))

        // --- Health Metrics & Projections ---
        .route("/savings-optimization", get(routes::analytics::handle_savings_optimization))
        .route("/net-worth", get(routes::analytics::handle_net_worth))
        .route("/health-score", get(routes::analytics::handle_financial_health_score))
        .route("/spending-patterns", get(routes::analytics::handle_spending_patterns))
        .route("/goal-projection", get(routes::analytics::handle_goal_projection))
        // Attaches and shares the PgPool explicitly to all handlers inside this specific router scope
        .with_state(pool)
}

#[tokio::main]
async fn main() {
    let db_ctx = DbContext::init().await.expect("Failed to initialize Supabase connection pool");
    println!("Successfully connected to Supabase Postgres!");

    // Extract and clone the PgPool out of your DbContext instance ◄─ ADD THIS LINE
    let pool = db_ctx.pool.clone();

    let app = Router::new()
        // Unprotected Public Auth Routes
        .route("/api/signup", post(routes::auth::signup))
        .route("/api/login", post(routes::auth::login))
        .route("/api/google-auth", post(routes::auth::google_auth))
        // Public Feedback Route
        .route("/api/feedback", post(routes::feedback::submit_feedback))

        .route("/api/transaction/test", get(routes::transactions::test_route))
        .route(
            "/api/transaction",
            post(routes::transactions::create_transaction).get(
                routes::transactions::get_transactions
            )
        )
        .route("/api/transaction/:id", delete(routes::transactions::delete_transaction))
        .route("/api/transaction/receipt/:id", get(routes::transactions::get_receipt))

        .route("/api/emi/calculate", post(routes::emi::emi_calculate))
        .route("/api/emi/check", post(routes::emi::emi_check))
        .route("/api/emi/create", post(routes::emi::emi_create))
        .route("/api/emi/:id", delete(routes::emi::emi_delete))
        // Protected Core Routes
        .route("/api/v1/finance/summary", get(get_protected_finance_summary))

        // 3. Nest the entire Analytics Sub-Router under the unified `/api/analytics` endpoint scope
        .nest("/api/analytics", build_analytics_router(pool)) // ◄─ Works perfectly now!

        // ◄─ Budget Routing Structure Added Here
        .route(
            "/api/budget",
            post(routes::budget::create_or_update_budget).get(routes::budget::get_budgets)
        )
        .route("/api/budget/:id", delete(routes::budget::delete_budget))
        .route("/api/budget/summary", get(routes::budget::budget_summary))
        // Add your DB Context as an extraction extension layer
        .layer(Extension(db_ctx));

    let listener = tokio::net::TcpListener::bind("127.0.0.1:4000").await.unwrap();
    println!("🚀 Axum core gateway processing on http://localhost:4000");
    axum::serve(listener, app).await.unwrap();
}
