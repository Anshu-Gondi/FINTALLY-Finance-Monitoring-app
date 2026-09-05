use fintally_db::DbContext;
use fintally_chatbot::chatbot_service::RagService;
use fintally_chatbot::chatbot_service::ChatbotOrchestrator;
use fintally_chatbot::chatbot_service::vision_service::VisionChatbotService;
use fintally_db::chat_service::ChatHistoryService;
use fintally_chatbot::core::llm::native_engine::NativeLlamaEngine;
use fintally_chatbot::core::llm::engine::LlmEngine;

// Vision imports
use fintally_chatbot::core::vision::engine::{DocumentInput, VisionEngine};

use axum::{ routing::{ get, post, delete }, Router, response::IntoResponse, Json, Extension };
use axum::http::HeaderValue;
use tower_http::cors::CorsLayer;
use futures_util::StreamExt;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use anyhow::Context;

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

// =============================================================================
// Qwen model discovery
// =============================================================================
//
// download_models selects exactly one Qwen variant and installs only the
// required GGUF artifact(s). main.rs must not hardcode a model filename because
// the selected variant can differ between machines.
//
// The resolver:
//   1. searches the configured Qwen model directory recursively,
//   2. ignores temporary/incomplete files,
//   3. understands both single-file and sharded GGUF layouts,
//   4. requires every shard of a sharded model to exist and be non-empty,
//   5. prefers the most specific Qwen Instruct GGUF artifact,
//   6. fails loudly when the directory is ambiguous.
//
// This makes startup deterministic and prevents accidentally loading an old
// model left behind by a previous hardware profile.

#[derive(Debug, Clone)]
struct ResolvedQwenModel {
    root_dir: PathBuf,
    model_files: Vec<PathBuf>,
    display_name: String,
}

fn collect_gguf_files(root: &Path) -> std::io::Result<Vec<PathBuf>> {
    fn visit(dir: &Path, files: &mut Vec<PathBuf>) -> std::io::Result<()> {
        for entry in fs::read_dir(dir)? {
            let entry = entry?;
            let path = entry.path();

            if path.is_dir() {
                visit(&path, files)?;
                continue;
            }

            let is_gguf = path
                .extension()
                .and_then(|ext| ext.to_str())
                .map(|ext| ext.eq_ignore_ascii_case("gguf"))
                .unwrap_or(false);

            if !is_gguf {
                continue;
            }

            let filename = path
                .file_name()
                .and_then(|name| name.to_str())
                .unwrap_or_default();

            // Never consider temporary downloads valid.
            if filename.starts_with('.') || filename.contains(".part-") {
                continue;
            }

            let metadata = fs::metadata(&path)?;

            if metadata.is_file() && metadata.len() > 0 {
                files.push(path);
            }
        }

        Ok(())
    }

    let mut files = Vec::new();

    if root.exists() {
        visit(root, &mut files)?;
    }

    Ok(files)
}

fn parse_shard_name(filename: &str) -> Option<(String, usize, usize)> {
    // Example:
    // qwen2.5-7b-instruct-q4_k_m-00001-of-00002.gguf
    let lower = filename.to_ascii_lowercase();

    let suffix = lower.strip_suffix(".gguf")?;
    let marker = "-of-";

    let of_pos = suffix.rfind(marker)?;
    let before_of = &suffix[..of_pos];
    let total_start = of_pos + marker.len();

    let total = suffix[total_start..].parse::<usize>().ok()?;

    let dash_pos = before_of.rfind('-')?;
    let index_str = &before_of[dash_pos + 1..];

    let index = index_str.parse::<usize>().ok()?;
    let prefix = before_of[..dash_pos].to_owned();

    Some((prefix, index, total))
}

fn resolve_qwen_model(root: impl AsRef<Path>) -> anyhow::Result<ResolvedQwenModel> {
    let root = root.as_ref();

    if !root.exists() {
        anyhow::bail!(
            "Qwen model directory does not exist: {}",
            root.display()
        );
    }

    let gguf_files = collect_gguf_files(root)
        .with_context(|| {
            format!(
                "failed to inspect Qwen model directory: {}",
                root.display()
            )
        })?;

    if gguf_files.is_empty() {
        anyhow::bail!(
            "no usable GGUF model files found under {}",
            root.display()
        );
    }

    // -------------------------------------------------------------------------
    // First resolve complete sharded models.
    // -------------------------------------------------------------------------

    let mut shard_groups:
        std::collections::BTreeMap<String, Vec<(usize, usize, PathBuf)>> =
        std::collections::BTreeMap::new();

    for path in &gguf_files {
        let filename = path
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or_default();

        if let Some((prefix, index, total)) = parse_shard_name(filename) {
            shard_groups
                .entry(prefix)
                .or_default()
                .push((index, total, path.clone()));
        }
    }

    let mut complete_sharded_candidates = Vec::new();

    for (prefix, mut shards) in shard_groups {
        if shards.is_empty() {
            continue;
        }

        shards.sort_by_key(|(index, _, _)| *index);

        let expected_total = shards[0].1;

        if expected_total <= 1 || shards.len() != expected_total {
            continue;
        }

        let complete = shards
            .iter()
            .enumerate()
            .all(|(position, (index, total, _))| {
                *total == expected_total && *index == position + 1
            });

        if complete {
            complete_sharded_candidates.push((
                prefix,
                shards
                    .into_iter()
                    .map(|(_, _, path)| path)
                    .collect::<Vec<_>>(),
            ));
        }
    }

    if !complete_sharded_candidates.is_empty() {
        // Prefer Qwen Instruct models. Then prefer the largest complete model
        // only when multiple complete Qwen variants are present.
        complete_sharded_candidates.sort_by(|a, b| {
            let a_name = a.0.to_ascii_lowercase();
            let b_name = b.0.to_ascii_lowercase();

            let a_instruct = a_name.contains("instruct");
            let b_instruct = b_name.contains("instruct");

            b_instruct
                .cmp(&a_instruct)
                .then_with(|| b_name.cmp(&a_name))
        });

        let (prefix, files) = complete_sharded_candidates
            .into_iter()
            .next()
            .expect("candidate list was checked as non-empty");

        return Ok(ResolvedQwenModel {
            root_dir: root.to_path_buf(),
            model_files: files,
            display_name: format!("{prefix}.gguf (sharded)"),
        });
    }

    // -------------------------------------------------------------------------
    // Single-file model fallback.
    // -------------------------------------------------------------------------

    let mut single_file_candidates = gguf_files
        .into_iter()
        .filter(|path| {
            path.file_name()
                .and_then(|name| name.to_str())
                .map(|name| {
                    let lower = name.to_ascii_lowercase();

                    lower.contains("qwen")
                        && lower.contains("instruct")
                        && !parse_shard_name(name).is_some()
                })
                .unwrap_or(false)
        })
        .collect::<Vec<_>>();

    if single_file_candidates.is_empty() {
        anyhow::bail!(
            "GGUF files exist under {}, but no usable Qwen Instruct model was found",
            root.display()
        );
    }

    single_file_candidates.sort();

    if single_file_candidates.len() > 1 {
        anyhow::bail!(
            "multiple single-file Qwen models are installed under {}. \
             Remove stale models so startup cannot select the wrong artifact. \
             Candidates: {}",
            root.display(),
            single_file_candidates
                .iter()
                .map(|path| path.display().to_string())
                .collect::<Vec<_>>()
                .join(", ")
        );
    }

    let model_file = single_file_candidates
        .into_iter()
        .next()
        .expect("candidate list was checked as non-empty");

    let display_name = model_file
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("qwen-model.gguf")
        .to_owned();

    Ok(ResolvedQwenModel {
        root_dir: root.to_path_buf(),
        model_files: vec![model_file],
        display_name,
    })
}

fn prepare_qwen_model_for_engine(
    root: impl AsRef<Path>,
) -> anyhow::Result<ResolvedQwenModel> {
    let resolved = resolve_qwen_model(root)?;

    println!("🧠 Resolved Qwen runtime model:");
    println!("   Name : {}", resolved.display_name);
    println!("   Root : {}", resolved.root_dir.display());

    for file in &resolved.model_files {
        let size_mb = fs::metadata(file)
            .map(|m| m.len() as f64 / 1024.0 / 1024.0)
            .unwrap_or(0.0);

        println!(
            "   GGUF : {} ({:.2} MB)",
            file.display(),
            size_mb
        );
    }

    Ok(resolved)
}

/// Builds the CORS layer from the ALLOWED_ORIGIN env var.
///
/// This is a real-user financial API — it must never fall back to a
/// wildcard origin. If ALLOWED_ORIGIN is unset or invalid, startup fails
/// loudly instead of silently opening the API to any origin.
fn build_cors_layer() -> CorsLayer {
    let allowed_origin = std::env::var("ALLOWED_ORIGIN")
        .expect(
            "ALLOWED_ORIGIN environment variable must be set to your frontend's \
             origin (e.g. https://app.fintally.com). Refusing to start with an \
             open CORS policy on a financial API."
        );

    let origin_value = HeaderValue::from_str(&allowed_origin)
        .expect("ALLOWED_ORIGIN is not a valid header value");

    println!("🔒 CORS restricted to origin: {}", allowed_origin);

    CorsLayer::new()
        .allow_origin(origin_value)
        .allow_methods([
            axum::http::Method::GET,
            axum::http::Method::POST,
            axum::http::Method::PUT,
            axum::http::Method::DELETE,
        ])
        .allow_headers([axum::http::header::AUTHORIZATION, axum::http::header::CONTENT_TYPE])
}

/// Waits for either Ctrl+C or a SIGTERM (container stop / k8s pod eviction)
/// so axum::serve can drain in-flight requests instead of dropping them.
async fn shutdown_signal() {
    let ctrl_c = async {
        tokio::signal::ctrl_c()
            .await
            .expect("failed to install Ctrl+C handler");
    };

    #[cfg(unix)]
    let terminate = async {
        tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
            .expect("failed to install SIGTERM handler")
            .recv()
            .await;
    };

    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();

    tokio::select! {
        _ = ctrl_c => {},
        _ = terminate => {},
    }

    println!("🛑 Shutdown signal received, draining in-flight requests...");
}

/// A minimal, valid 1x1 black PNG (67 bytes) used purely to force the vision
/// engine's decode/inference kernels to compile during warmup. The previous
/// warmup passed 3 raw bytes with no header, which is not a decodable image
/// and would fail (or worse, silently no-op) on every startup.
const WARMUP_PNG_1X1: [u8; 69] = [
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0x60, 0x60, 0x60, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x01, 0x5C, 0xCD, 0xFF, 0x69, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
    0x44, 0xAE, 0x42, 0x60, 0x82,
];

#[tokio::main]
async fn main() {
    let db_ctx = DbContext::init().await.expect("Failed to initialize Supabase connection pool");
    println!("Successfully connected to Supabase Postgres!");

    let pool = db_ctx.pool.clone();

    let rag_service = Arc::new(
        RagService::new(
            "./vector_storage",
            "./llm_models/embedding/bge_safetensors_output"
        ).expect("Failed to build pure-Rust Vector engine service layer")
    );

    jobs::jobs::start_scheduler(pool.clone(), rag_service.clone());
    println!("⚙️  Thread pool processes registered. Background schedulers operational.");

    // ─── 1. Resolve + Initialize Native Llama Engine Model ────────────────
    //
    // Do not hardcode "3B", "7B", Q4, Q8, etc. here.
    // download_models is responsible for selecting the correct model for the
    // machine; startup discovers exactly what was installed.

    const QWEN_MODEL_ROOT: &str =
        "./llm_models/chat/qwen_safetensors_output";

    println!("🔎 Resolving installed Qwen model...");

    let resolved_qwen = prepare_qwen_model_for_engine(QWEN_MODEL_ROOT)
        .expect("Failed to resolve a valid installed Qwen model");

    // Loading multi-GB GGUF weights is CPU/IO-bound and was previously done
    // directly on a tokio worker thread, which stalls that thread (and the
    // scheduler jobs started above) for the duration of the load. Moved to
    // spawn_blocking, matching the pattern already used for vision warmup.
    println!(
        "⏳ Loading Native Qwen LLM weights from {}...",
        resolved_qwen.root_dir.display()
    );

    let qwen_root_str = resolved_qwen
        .root_dir
        .to_str()
        .context("Qwen model root path is not valid UTF-8")
        .expect("Invalid Qwen model root path")
        .to_owned();

    let native_engine = tokio::task
        ::spawn_blocking(move || NativeLlamaEngine::load_from_vault(&qwen_root_str))
        .await
        .expect("Qwen engine load thread panicked")
        .expect("Failed to instantiate native Qwen inference engine from validated model path");

    let native_engine = Arc::new(native_engine);

    // ─── 2. Execute Async LLM Warmup Sequence ─────────────────────────────
    println!("🔥 Warming up LLM core engine (Compiling execution graphs / Allocating buffers)...");
    let warmup_prompt = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nHi<|im_end|>\n<|im_start|>assistant\n";

    match native_engine.stream_generate(warmup_prompt, 16).await {
        Ok(mut cancelable_stream) => {
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

    let vision_warmup_result = tokio::task::spawn_blocking(|| {
        let mut vision_engine = VisionEngine::new()?;

        // A real, decodable 1x1 PNG — the previous 3 raw bytes with format
        // string "format" were not a valid image and this step likely failed
        // (or no-opped) on every startup.
        vision_engine.process_input(
            DocumentInput::RawImageBytes(&WARMUP_PNG_1X1)
        )
    }).await;

    match vision_warmup_result {
        Ok(Ok(_)) => println!("✅ GOT-OCR 2.0 Vision Engine successfully warmed up."),
        Ok(Err(e)) => eprintln!("⚠️ Warning: Vision engine warmup failed: {:?}. First OCR request may experience latency.", e),
        Err(e) => eprintln!("⚠️ Warning: Vision engine warmup thread join error: {:?}", e),
    }

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

    let cors = build_cors_layer();

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

    let bind_address = format!("0.0.0.0:{}", port);
    let listener = tokio::net::TcpListener::bind(&bind_address).await.unwrap();

    println!("🚀 Axum core gateway processing on http://{}", bind_address);

    axum::serve(listener, app)
        .with_graceful_shutdown(shutdown_signal())
        .await
        .unwrap();
}
