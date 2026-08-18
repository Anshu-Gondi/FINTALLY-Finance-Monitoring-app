use axum::{
    extract::{Multipart, Query, State},
    http::StatusCode,
    response::{
        sse::{Event, Sse},
        IntoResponse,
    },
    routing::{delete, get, post},
    Json, Router,
};
use base64::engine::general_purpose::STANDARD as BASE64;
use base64::Engine as _;
use futures_util::StreamExt;
use serde::{Deserialize, Serialize};
use std::{convert::Infallible, sync::Arc};

use crate::auth::Claims;
use fintally_chatbot::chatbot_service::chatbot_orchestrator::{ChatbotOrchestrator, ChatMessage};
use fintally_chatbot::chatbot_service::vision_service::VisionAttachment;
use fintally_db::chat_service::ChatHistoryService;

/// Shared application state injected into the Axum pipeline
pub struct ChatState {
    pub orchestrator: Arc<ChatbotOrchestrator>,
    pub history_service: Arc<ChatHistoryService>,
}

#[derive(Debug, Deserialize)]
pub struct ChatAttachmentPayload {
    pub file_type: String, // "image", "pdf", or "chart"
    pub bytes_base64: String,
}

#[derive(Debug, Deserialize)]
pub struct ChatRequest {
    pub message: String,
    pub session_id: Option<i64>,
    #[serde(default = "default_max_tokens")]
    pub max_tokens: i64,
    #[serde(default)]
    pub attachments: Vec<ChatAttachmentPayload>,
}

fn default_max_tokens() -> i64 {
    512
}

#[derive(Debug, Serialize)]
pub struct ChatOnceResponse {
    pub reply: String,
    pub tool_called: Option<String>,
    pub tool_result: Option<serde_json::Value>,
}

pub fn chat_routes(state: Arc<ChatState>) -> Router {
    Router::new()
        .route("/", post(chat_stream_endpoint))
        .route("/once", post(chat_once_endpoint))
        .route("/multipart", post(chat_stream_multipart_endpoint)) // Route for direct file uploads
        .route("/history", get(get_chat_history_endpoint))
        .route("/history", delete(clear_chat_history_endpoint))
        .route("/sessions", get(get_sessions_endpoint))
        .route("/sessions/:session_id", delete(delete_session_endpoint))
        .with_state(state)
}

/// Helper function to validate and convert base64 payload attachments to VisionAttachments
fn parse_json_attachments(
    raw_attachments: Vec<ChatAttachmentPayload>,
) -> Result<Vec<VisionAttachment>, (StatusCode, &'static str)> {
    if raw_attachments.len() > 3 {
        return Err((
            StatusCode::BAD_REQUEST,
            "Maximum limit of 3 attachments exceeded.",
        ));
    }

    let mut attachments = Vec::new();
    for att in raw_attachments {
        // Updated to use modern base64 Engine API
        let bytes = match BASE64.decode(&att.bytes_base64) {
            Ok(b) => b,
            Err(_) => {
                return Err((
                    StatusCode::BAD_REQUEST,
                    "Invalid base64 encoding in attachment payload.",
                ))
            }
        };

        match att.file_type.to_lowercase().as_str() {
            "image" | "png" | "jpg" | "jpeg" => attachments.push(VisionAttachment::Image(bytes)),
            "pdf" => attachments.push(VisionAttachment::Pdf(bytes)),
            "chart" => attachments.push(VisionAttachment::Chart(bytes)),
            _ => {
                return Err((
                    StatusCode::BAD_REQUEST,
                    "Unsupported attachment file_type.",
                ))
            }
        }
    }

    Ok(attachments)
}

// ──────────────────────────────────────────────────────────────────────────────
// 1. Streaming Endpoint via Server-Sent Events (JSON with Base64 Attachments)
// ──────────────────────────────────────────────────────────────────────────────
async fn chat_stream_endpoint(
    State(state): State<Arc<ChatState>>,
    claims: Claims,
    Json(payload): Json<ChatRequest>,
) -> impl IntoResponse {
    let target_user_id = match claims.user_id.parse::<i64>() {
        Ok(parsed) => parsed,
        Err(_) => {
            return (
                StatusCode::BAD_REQUEST,
                "Invalid User ID format signature.",
            )
                .into_response()
        }
    };

    // Parse and enforce max 3 files limit
    let attachments = match parse_json_attachments(payload.attachments) {
        Ok(atts) => atts,
        Err((code, msg)) => return (code, msg).into_response(),
    };

    let history_rows = match state
        .history_service
        .get_history(target_user_id, payload.session_id, 20)
        .await
    {
        Ok(h) => h,
        Err(err) => {
            eprintln!("[DATABASE ERROR] Failed to fetch history: {:?}", err);
            Vec::new()
        }
    };

    let chat_history: Vec<ChatMessage> = history_rows
        .into_iter()
        .map(|m| ChatMessage {
            role: m.role,
            content: m.content,
        })
        .collect();

    if let Err(err) = state
        .history_service
        .append_message(
            target_user_id,
            "user",
            &payload.message,
            payload.session_id,
            None,
        )
        .await
    {
        eprintln!("[DATABASE ERROR] Failed to record user message: {:?}", err);
    }

    let raw_token_stream = state.orchestrator.clone().chat_stream(
        target_user_id,
        payload.message,
        chat_history,
        attachments,
    );
    let history_svc_clone = Arc::clone(&state.history_service);
    let session_id = payload.session_id;

    let sse_stream = async_stream::stream! {
        let mut assistant_response_accumulator = String::new();
        let mut detected_tool: Option<String> = None;
        let mut detected_tool_res: Option<serde_json::Value> = None;

        tokio::pin!(raw_token_stream);

        while let Some(item) = raw_token_stream.next().await {
            match item {
                Ok(token) => {
                    if token.starts_with("[TOOL_CALL:") {
                        detected_tool = Some(token.trim_start_matches("[TOOL_CALL:").trim_end_matches(']').to_string());
                    } else if token.starts_with("[TOOL_RESULT:") {
                        let raw_json = token.trim_start_matches("[TOOL_RESULT:").trim_end_matches(']');
                        if let Ok(parsed) = serde_json::from_str::<serde_json::Value>(raw_json) {
                            detected_tool_res = Some(parsed);
                        }
                    } else if !token.starts_with('[') {
                        assistant_response_accumulator.push_str(&token);
                    }

                    let formatted_token = token.replace('\n', "\\n");
                    yield Ok::<Event, Infallible>(Event::default().data(formatted_token));
                }
                Err(e) => {
                    eprintln!("[ORCHESTRATOR STREAM ERROR]: {:?}", e);
                    yield Ok::<Event, Infallible>(Event::default().data(format!("[ERROR: {}]", e)));
                }
            }
        }

        if !assistant_response_accumulator.is_empty() {
            let mut metadata = serde_json::json!({});
            if let Some(t) = detected_tool {
                metadata["tool_called"] = serde_json::Value::String(t);
            }
            if let Some(r) = detected_tool_res {
                metadata["tool_result"] = r;
            }

            let _ = history_svc_clone.append_message(
                target_user_id,
                "assistant",
                &assistant_response_accumulator,
                session_id,
                Some(metadata)
            ).await;
        }

        yield Ok::<Event, Infallible>(Event::default().data("[DONE]"));
    };

    Sse::new(sse_stream)
        .keep_alive(axum::response::sse::KeepAlive::new())
        .into_response()
}

// ──────────────────────────────────────────────────────────────────────────────
// 2. Streaming Endpoint via Multipart Form Data (Direct File Uploads)
// ──────────────────────────────────────────────────────────────────────────────
async fn chat_stream_multipart_endpoint(
    State(state): State<Arc<ChatState>>,
    claims: Claims,
    mut multipart: Multipart,
) -> impl IntoResponse {
    let target_user_id = match claims.user_id.parse::<i64>() {
        Ok(parsed) => parsed,
        Err(_) => {
            return (
                StatusCode::BAD_REQUEST,
                "Invalid User ID format signature.",
            )
                .into_response()
        }
    };

    let mut message = String::new();
    let mut session_id = None;
    let mut attachments = Vec::new();

    while let Ok(Some(field)) = multipart.next_field().await {
        let field_name = field.name().unwrap_or_default().to_string();

        match field_name.as_str() {
            "message" => {
                if let Ok(txt) = field.text().await {
                    message = txt;
                }
            }
            "session_id" => {
                if let Ok(txt) = field.text().await {
                    session_id = txt.parse::<i64>().ok();
                }
            }
            "files" | "attachment" => {
                if attachments.len() >= 3 {
                    return (
                        StatusCode::BAD_REQUEST,
                        "A maximum of 3 attachments are allowed per query.",
                    )
                        .into_response();
                }

                let content_type = field
                    .content_type()
                    .unwrap_or("application/octet-stream")
                    .to_string();
                if let Ok(bytes) = field.bytes().await {
                    let vec_bytes = bytes.to_vec();
                    if content_type.contains("pdf") {
                        attachments.push(VisionAttachment::Pdf(vec_bytes));
                    } else if content_type.contains("chart") {
                        attachments.push(VisionAttachment::Chart(vec_bytes));
                    } else {
                        attachments.push(VisionAttachment::Image(vec_bytes));
                    }
                }
            }
            _ => {}
        }
    }

    let history_rows = match state
        .history_service
        .get_history(target_user_id, session_id, 20)
        .await
    {
        Ok(h) => h,
        Err(_) => Vec::new(),
    };

    let chat_history: Vec<ChatMessage> = history_rows
        .into_iter()
        .map(|m| ChatMessage {
            role: m.role,
            content: m.content,
        })
        .collect();

    let _ = state
        .history_service
        .append_message(target_user_id, "user", &message, session_id, None)
        .await;

    let raw_token_stream = state.orchestrator.clone().chat_stream(
        target_user_id,
        message,
        chat_history,
        attachments,
    );

    let sse_stream = async_stream::stream! {
        tokio::pin!(raw_token_stream);

        while let Some(item) = raw_token_stream.next().await {
            if let Ok(token) = item {
                let formatted_token = token.replace('\n', "\\n");
                yield Ok::<Event, Infallible>(Event::default().data(formatted_token));
            }
        }
        yield Ok::<Event, Infallible>(Event::default().data("[DONE]"));
    };

    Sse::new(sse_stream)
        .keep_alive(axum::response::sse::KeepAlive::new())
        .into_response()
}

// ──────────────────────────────────────────────────────────────────────────────
// 3. Non-Streaming Endpoint (Stateless REST / Postman)
// ──────────────────────────────────────────────────────────────────────────────
async fn chat_once_endpoint(
    State(state): State<Arc<ChatState>>,
    claims: Claims,
    Json(payload): Json<ChatRequest>,
) -> impl IntoResponse {
    let target_user_id = match claims.user_id.parse::<i64>() {
        Ok(parsed) => parsed,
        Err(_) => {
            return (
                StatusCode::BAD_REQUEST,
                "Invalid User ID format signature.",
            )
                .into_response()
        }
    };

    let attachments = match parse_json_attachments(payload.attachments) {
        Ok(atts) => atts,
        Err((code, msg)) => return (code, msg).into_response(),
    };

    let history_rows = match state
        .history_service
        .get_history(target_user_id, payload.session_id, 20)
        .await
    {
        Ok(h) => h,
        Err(_) => return StatusCode::INTERNAL_SERVER_ERROR.into_response(),
    };

    let chat_history: Vec<ChatMessage> = history_rows
        .into_iter()
        .map(|m| ChatMessage {
            role: m.role,
            content: m.content,
        })
        .collect();

    let _ = state
        .history_service
        .append_message(
            target_user_id,
            "user",
            &payload.message,
            payload.session_id,
            None,
        )
        .await;

    let complete_stream = state.orchestrator.clone().chat_stream(
        target_user_id,
        payload.message,
        chat_history,
        attachments,
    );
    let mut final_reply = String::new();
    let mut tool_called = None;
    let mut tool_result = None;

    tokio::pin!(complete_stream);

    while let Some(item) = complete_stream.next().await {
        if let Ok(chunk) = item {
            if chunk.starts_with("[TOOL_CALL:") {
                tool_called = Some(
                    chunk
                        .trim_start_matches("[TOOL_CALL:")
                        .trim_end_matches(']')
                        .to_string(),
                );
            } else if chunk.starts_with("[TOOL_RESULT:") {
                let raw_json = chunk
                    .trim_start_matches("[TOOL_RESULT:")
                    .trim_end_matches(']');
                tool_result = serde_json::from_str::<serde_json::Value>(raw_json).ok();
            } else if !chunk.starts_with('[') {
                final_reply.push_str(&chunk);
            }
        }
    }

    let mut metadata = serde_json::json!({});
    if let Some(ref t) = tool_called {
        metadata["tool_called"] = serde_json::Value::String(t.clone());
    }
    if let Some(ref r) = tool_result {
        metadata["tool_result"] = r.clone();
    }

    let _ = state
        .history_service
        .append_message(
            target_user_id,
            "assistant",
            &final_reply,
            payload.session_id,
            Some(metadata),
        )
        .await;

    Json(ChatOnceResponse {
        reply: final_reply,
        tool_called,
        tool_result,
    })
    .into_response()
}

// ──────────────────────────────────────────────────────────────────────────────
// 4. Management Endpoints (History fetching, Sessions listing & Purging)
// ──────────────────────────────────────────────────────────────────────────────
#[derive(Deserialize)]
struct HistoryQuery {
    session_id: Option<i64>,
    #[serde(default = "default_limit")]
    limit: i64,
}
fn default_limit() -> i64 {
    50
}

async fn get_chat_history_endpoint(
    State(state): State<Arc<ChatState>>,
    claims: Claims,
    Query(query): Query<HistoryQuery>,
) -> impl IntoResponse {
    let user_id: i64 = match claims.user_id.parse() {
        Ok(id) => id,
        Err(_) => return StatusCode::BAD_REQUEST.into_response(),
    };

    match state
        .history_service
        .get_full_history(user_id, query.session_id, query.limit)
        .await
    {
        Ok(history) => {
            let count = history.len();
            (
                StatusCode::OK,
                Json(serde_json::json!({ "history": history, "count": count })),
            )
                .into_response()
        }
        Err(_) => StatusCode::INTERNAL_SERVER_ERROR.into_response(),
    }
}

async fn get_sessions_endpoint(
    State(state): State<Arc<ChatState>>,
    claims: Claims,
) -> impl IntoResponse {
    let user_id: i64 = match claims.user_id.parse() {
        Ok(id) => id,
        Err(_) => return StatusCode::BAD_REQUEST.into_response(),
    };

    match state.history_service.get_all_sessions(user_id).await {
        Ok(sessions) => (
            StatusCode::OK,
            Json(serde_json::json!({ "sessions": sessions })),
        )
            .into_response(),
        Err(_) => StatusCode::INTERNAL_SERVER_ERROR.into_response(),
    }
}

async fn clear_chat_history_endpoint(
    State(state): State<Arc<ChatState>>,
    claims: Claims,
    Query(query): Query<HistoryQuery>,
) -> impl IntoResponse {
    let user_id: i64 = match claims.user_id.parse() {
        Ok(id) => id,
        Err(_) => return StatusCode::BAD_REQUEST.into_response(),
    };

    match state
        .history_service
        .clear_history(user_id, query.session_id)
        .await
    {
        Ok(_) => (
            StatusCode::OK,
            Json(serde_json::json!({ "message": "Chat history cleared successfully" })),
        )
            .into_response(),
        Err(_) => StatusCode::INTERNAL_SERVER_ERROR.into_response(),
    }
}

async fn delete_session_endpoint(
    State(state): State<Arc<ChatState>>,
    claims: Claims,
    axum::extract::Path(session_id): axum::extract::Path<i64>,
) -> impl IntoResponse {
    let user_id: i64 = match claims.user_id.parse() {
        Ok(id) => id,
        Err(_) => return StatusCode::BAD_REQUEST.into_response(),
    };

    match state
        .history_service
        .clear_history(user_id, Some(session_id))
        .await
    {
        Ok(_) => (
            StatusCode::OK,
            Json(serde_json::json!({ "message": "Session deleted successfully" })),
        )
            .into_response(),
        Err(_) => StatusCode::INTERNAL_SERVER_ERROR.into_response(),
    }
}
