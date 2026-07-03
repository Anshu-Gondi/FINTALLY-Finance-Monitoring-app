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

#[tokio::main]
async fn main() {
    let db_ctx = DbContext::init().await.expect("Failed to initialize Supabase connection pool");
    println!("Successfully connected to Supabase Postgres!");

    let app = Router::new()
        // Unprotected Public Auth Routes
        .route("/api/signup", post(routes::auth::signup))
        .route("/api/login", post(routes::auth::login))
        .route("/api/google-auth", post(routes::auth::google_auth))
        // Public Feedback Route
        .route("/api/feedback", post(routes::feedback::submit_feedback))

        .route("/api/transaction/test", get(routes::transactions::test_route))
        .route("/api/transaction", post(routes::transactions::create_transaction).get(routes::transactions::get_transactions))
        .route("/api/transaction/:id", delete(routes::transactions::delete_transaction))
        .route("/api/transaction/receipt/:id", get(routes::transactions::get_receipt))


        .route("/api/emi/calculate", post(routes::emi::emi_calculate))
        .route("/api/emi/check", post(routes::emi::emi_check))
        .route("/api/emi/create", post(routes::emi::emi_create))
        .route("/api/emi/:id", delete(routes::emi::emi_delete))
        // Protected Core Routes
        .route("/api/v1/finance/summary", get(get_protected_finance_summary))
        // Add your DB Context as an extraction extension layer
        .layer(Extension(db_ctx));

    let listener = tokio::net::TcpListener::bind("127.0.0.1:4000").await.unwrap();
    println!("🚀 Axum core gateway processing on http://localhost:4000");
    axum::serve(listener, app).await.unwrap();
}
