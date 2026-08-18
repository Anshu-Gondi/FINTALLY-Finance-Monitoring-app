use fintally_db::DbContext;
use fintally_chatbot::chatbot_service::RagService;
// Correct structural paths for the service layer
use fintally_chatbot::chatbot_service::ChatbotOrchestrator;
use fintally_chatbot::chatbot_service::vision_service::VisionChatbotService; // ◄── Added VisionChatbotService import
use fintally_db::chat_service::ChatHistoryService;
use fintally_chatbot::core::llm::native_engine::NativeLlamaEngine;
use fintally_chatbot::core::llm::engine::LlmEngine;

// Vision imports
use fintally_chatbot::core::vision::engine::{DocumentInput, VisionEngine};

use axum::{ routing::{ get, post, delete }, Router, response::IntoResponse, Json, Extension };
use tower_http::cors::{ CorsLayer, Any };
use futures_util::StreamExt;
use std::sync::Arc;

pub mod auth;
pub mod routes;
pub mod jobs;

use crate::routes::chat::{ chat_routes, ChatState };
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

async fn manual_rag_sync_handler(
    claims: Claims,
    Extension(rag_service): Extension<Arc<RagService>>
) -> impl IntoResponse {
    println!(
        "🔐 Authenticated RAG Engine Synchronization run requested by User: {}",
        claims.user_id
    );

    match rag_service.sync_and_reindex_trusted_sources().await {
        Ok(_) =>
            (
                axum::http::StatusCode::OK,
                Json(
                    serde_json::json!({
                        "status": "success",
                        "message": "Pure Rust Google Drive indexing completed successfully."
                    })
                ),
            ),
        Err(err) =>
            (
                axum::http::StatusCode::INTERNAL_SERVER_ERROR,
                Json(
                    serde_json::json!({
                        "status": "error",
                        "detail": format!("{:?}", err)
                    })
                ),
            ),
    }
}

fn build_analytics_router(pool: sqlx::PgPool) -> Router {
    Router::new()
        .route("/daily", get(routes::analytics::handle_daily_summary))
        .route("/period", get(routes::analytics::handle_period_summary))
        .route("/lifetime", get(routes::analytics::handle_lifetime_analysis))
        .route("/min-max", get(routes::analytics::handle_min_max_transaction))
        .route("/category", get(routes::analytics::handle_category_summary))
        .route("/trend", get(routes::analytics::handle_trend_summary))
        .route("/emi-pressure", get(routes::analytics::handle_emi_pressure))
        .route("/cashflow-forecast", get(routes::analytics::handle_cashflow_forecast))
        .route("/budget-breach", get(routes::analytics::handle_budget_breach_prediction))
        .route("/anomalies/recurring", get(routes::analytics::handle_recurring_anomalies))
        .route("/anomalies/transactions", get(routes::analytics::handle_transaction_anomalies))
        .route("/drift", get(routes::analytics::handle_category_drift))
        .route("/recurring-impact", get(routes::analytics::handle_recurring_impact))
        .route("/utilization", get(routes::analytics::handle_budget_utilization))
        .route("/burn-rate", get(routes::analytics::handle_burn_rate))
        .route("/income-stability", get(routes::analytics::handle_income_stability))
        .route("/savings-optimization", get(routes::analytics::handle_savings_optimization))
        .route("/net-worth", get(routes::analytics::handle_net_worth))
        .route("/health-score", get(routes::analytics::handle_financial_health_score))
        .route("/spending-patterns", get(routes::analytics::handle_spending_patterns))
        .route("/goal-projection", get(routes::analytics::handle_goal_projection))
        .with_state(pool)
}

#[tokio::main]
async fn main() {
    let db_ctx = DbContext::init().await.expect("Failed to initialize Supabase connection pool");
    println!("Successfully connected to Supabase Postgres!");

    let pool = db_ctx.pool.clone();

    let rag_service = Arc::new(
        RagService::new(
            "./vector_storage",
            "./llm_models/embedding/bge_safetensors_output" // ◄── Path to your embedding model directory containing config.json
        ).expect("Failed to build pure-Rust Vector engine service layer")
    );

    jobs::jobs::start_scheduler(pool.clone(), rag_service.clone());
    println!("⚙️  Thread pool processes registered. Background schedulers operational.");

    // ─── 1. Initialize Native Llama Engine Model ────────────────────────────
    println!("⏳ Loading Native Qwen LLM weights into system memory...");

    let native_engine = Arc::new(
        NativeLlamaEngine::load_from_vault("./llm_models/chat/qwen_safetensors_output").expect(
            "Failed to instantiate native Qwen inference engine from specified path layout"
        )
    );

    // ─── 2. Execute Async LLM Warmup Sequence ─────────────────────────────
    println!("🔥 Warming up LLM core engine (Compiling execution graphs / Allocating buffers)...");
    let warmup_prompt = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nHi<|im_end|>\n<|im_start|>assistant\n";

    match native_engine.stream_generate(warmup_prompt, 16).await {
        Ok(mut cancelable_stream) => {
            // Consume tokens silently to force processing pipeline compilation
            while let Some(Ok(_token)) = cancelable_stream.stream.next().await {}
            println!("✅ LLM memory compilation pathways successfully primed.");
        }
        Err(e) => {
            eprintln!(
                "⚠️ Warning: Model warmup sequence failed: {:?}. System will continue, but first prompt response could be sluggish.",
                e
            );
        }
    }

    // ─── 3. Execute Async GOT-OCR 2.0 Vision Engine Warmup Sequence ─────────
    println!("👁️ Warming up GOT-OCR 2.0 Vision Engine (Pre-allocating C++ FFI buffers & CUDA tensors)...");

    // Non-blocking offload for Vision Engine instantiation and dummy pass
    let vision_warmup_result = tokio::task::spawn_blocking(|| {
        // Uses default path "llm_models/ocr/got_ocr2_0_output"
        let mut vision_engine = VisionEngine::new()?;

        // Minimal 1x1 black image buffer to compile Candle Vision CUDA/CPU kernels
        let dummy_pixel_bytes: [u8; 3] = [0, 0, 0];

        vision_engine.process_input(
            DocumentInput::RawImageBytes(&dummy_pixel_bytes),
            "format"
        )
    }).await;

    match vision_warmup_result {
        Ok(Ok(_)) => println!("✅ GOT-OCR 2.0 Vision Engine successfully warmed up."),
        Ok(Err(e)) => eprintln!("⚠️ Warning: Vision engine warmup failed: {:?}. First OCR request may experience latency.", e),
        Err(e) => eprintln!("⚠️ Warning: Vision engine warmup thread join error: {:?}", e),
    }

    // Instantiate Vision Chatbot Service
    let vision_service = Arc::new(VisionChatbotService::new());

    // ─── 4. Instantiate Orchestrator and Context States ────────────────────
    let orchestrator = Arc::new(
        ChatbotOrchestrator::new(pool.clone(), rag_service.clone(), vision_service, native_engine)
    );
    let db_context = DbContext { pool: pool.clone() };
    let history_service = Arc::new(ChatHistoryService::new(db_context));

    let chat_state = Arc::new(ChatState {
        orchestrator,
        history_service,
    });

    // ─── 5. Build Unified Core App Router ────────────────────────────────────

    let cors = CorsLayer::new().allow_origin(Any).allow_methods(Any).allow_headers(Any);

    let app = Router::new()
        .route("/api/signup", post(routes::auth::signup))
        .route("/api/login", post(routes::auth::login))
        .route("/api/google-auth", post(routes::auth::google_auth))
        .route("/api/feedback", post(routes::feedback::submit_feedback))
        .route("/api/transaction/test", get(routes::transactions::test_route))
        .route(
            "/api/transaction",
            post(routes::transactions::create_transaction).get(
                routes::transactions::get_transactions
            )
        )
        .route(
            "/api/transaction/:id",
            delete(routes::transactions::delete_transaction).put(
                routes::transactions::update_transaction
            )
        )
        .route("/api/transaction/receipt/:id", get(routes::transactions::get_receipt))
        .route("/api/emi/calculate", post(routes::emi::emi_calculate))
        .route("/api/emi/check", post(routes::emi::emi_check))
        .route("/api/emi/create", post(routes::emi::emi_create))
        .route("/api/emi/:id", delete(routes::emi::emi_delete))
        .route("/api/v1/finance/summary", get(get_protected_finance_summary))
        .route("/api/rag/sync", post(manual_rag_sync_handler))
        .nest("/api/analytics", build_analytics_router(pool))
        .nest("/api/chat", chat_routes(chat_state))
        .route(
            "/api/budget",
            post(routes::budget::create_or_update_budget).get(routes::budget::get_budgets)
        )
        .route("/api/budget/:id", delete(routes::budget::delete_budget))
        .route("/api/budget/summary", get(routes::budget::budget_summary))
        .layer(cors)
        .layer(Extension(db_ctx))
        .layer(Extension(rag_service));

    let port = std::env
        ::var("PORT")
        .unwrap_or_else(|_| "8080".to_string())
        .parse::<u16>()
        .expect("PORT environment variable must be a valid number");

    // Bind to 0.0.0.0 so the container handles traffic routed from outside
    let bind_address = format!("0.0.0.0:{}", port);
    let listener = tokio::net::TcpListener::bind(&bind_address).await.unwrap();

    println!("🚀 Axum core gateway processing on http://{}", bind_address);
    axum::serve(listener, app).await.unwrap();
}
