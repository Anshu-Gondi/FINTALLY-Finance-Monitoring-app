use axum::{
    http::StatusCode,
    response::IntoResponse,
    Extension, Json,
};
use serde::Serialize;
use fintally_db::DbContext;

// Importing the FeedbackCreate schema model you shared earlier
use fintally_db::models::FeedbackCreate;

#[derive(Serialize)]
pub struct FeedbackResponse {
    pub success: bool,
    pub message: String,
}

// Custom internal error structure for clean API failures
#[derive(Debug)]
pub enum FeedbackRouteError {
    DatabaseError(String),
}

impl IntoResponse for FeedbackRouteError {
    fn into_response(self) -> axum::response::Response {
        let (status, err_msg) = match self {
            FeedbackRouteError::DatabaseError(msg) => (StatusCode::INTERNAL_SERVER_ERROR, msg),
        };
        
        let body = Json(serde_json::json!({
            "error": "FeedbackError",
            "detail": err_msg
        }));

        (status, body).into_response()
    }
}

// ──────────────────────────────────────────────
// POST /api/feedback
// ──────────────────────────────────────────────
pub async fn submit_feedback(
    Extension(db_ctx): Extension<DbContext>,
    Json(body): Json<FeedbackCreate>,
) -> Result<impl IntoResponse, FeedbackRouteError> {
    
    // Insert structured payload safely into Postgres
    sqlx::query!(
        "INSERT INTO feedbacks (name, email, message) VALUES ($1, $2, $3)",
        body.name,
        body.email,
        body.message
    )
    .execute(&db_ctx.pool)
    .await
    .map_err(|e| FeedbackRouteError::DatabaseError(e.to_string()))?;

    Ok((
        StatusCode::CREATED,
        Json(FeedbackResponse {
            success: true,
            message: "Feedback submitted successfully".to_string(),
        }),
    ))
}