use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Extension, Json,
};
use bcrypt::{hash, verify, DEFAULT_COST};
use jsonwebtoken::{encode, EncodingKey, Header};
use serde::{Deserialize, Serialize};
use std::env;
use std::time::{SystemTime, UNIX_EPOCH};
use google_jwt_signin::Client as GoogleClient;

use fintally_db::DbContext;

// --- DTOs Matching Your Domain Models ---

#[derive(Deserialize)]
pub struct SignupRequest {
    pub name: String,
    pub email: String,
    pub password: String,
}

#[derive(Deserialize)]
pub struct LoginRequest {
    pub email: String,
    pub password: String,
}

#[derive(Serialize)]
pub struct AuthResponse {
    pub success: bool,
    pub message: Option<String>,
    pub token: Option<String>,
}

#[derive(Serialize)]
struct ApiErrorResponse {
    error: String,
    detail: String,
}

#[derive(Deserialize)]
pub struct GoogleAuthRequest {
    pub token: String,
}

// Custom error type for authentication routes
#[derive(Debug)]
pub enum AuthRouteError {
    EmailExists,
    InvalidCredentials,
    DatabaseError(String),
}

impl IntoResponse for AuthRouteError {
    fn into_response(self) -> Response {
        let (status, detail) = match self {
            AuthRouteError::EmailExists => (StatusCode::BAD_REQUEST, "Email already registered".to_string()),
            AuthRouteError::InvalidCredentials => (StatusCode::UNAUTHORIZED, "Invalid email or password".to_string()),
            AuthRouteError::DatabaseError(msg) => (StatusCode::INTERNAL_SERVER_ERROR, msg),
        };

        let body = Json(ApiErrorResponse {
            error: "AuthError".to_string(),
            detail,
        });

        (status, body).into_response()
    }
}

// --- JWT Generation Utility ---
pub fn make_token(user_id: &str) -> Result<String, String> {
    // 1. Fetch secret (fail closed if missing, matching auth.rs)
    let secret = env::var("JWT_SECRET")
        .map_err(|_| "JWT_SECRET environment variable is not configured".to_string())?;

    // 2. Fetch current Unix timestamp safely
    let iat = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| "System time precedes UNIX epoch".to_string())?
        .as_secs();

    // 3. Set expiration (e.g., 7 days = 604,800 seconds)
    let exp = iat + (7 * 24 * 60 * 60);

    // 4. Construct Claims using the correct variable names
    let claims = crate::auth::Claims {
        user_id: user_id.to_string(),
        iat,
        exp,
    };

    // 5. Mint and sign token
    encode(
        &Header::default(),
        &claims,
        &EncodingKey::from_secret(secret.as_bytes()),
    )
    .map_err(|e| format!("Failed to sign authentication token: {e}"))
}

// --- Route Handlers ---

pub async fn signup(
    Extension(db_ctx): Extension<DbContext>,
    Json(body): Json<SignupRequest>,
) -> Result<impl IntoResponse, AuthRouteError> {
    let existing = sqlx::query!("SELECT id FROM users WHERE email = $1", body.email)
        .fetch_optional(&db_ctx.pool)
        .await
        .map_err(|e| AuthRouteError::DatabaseError(e.to_string()))?;

    if existing.is_some() {
        return Err(AuthRouteError::EmailExists);
    }

    let hashed_password = hash(body.password, DEFAULT_COST)
        .map_err(|e| AuthRouteError::DatabaseError(e.to_string()))?;

    // Notice we use the matching 'local' string representation castable to your database enum
    sqlx::query!(
        "INSERT INTO users (name, email, password, auth_provider) VALUES ($1, $2, $3, 'local')",
        body.name,
        body.email,
        hashed_password
    )
    .execute(&db_ctx.pool)
    .await
    .map_err(|e| AuthRouteError::DatabaseError(e.to_string()))?;

    Ok((
        StatusCode::CREATED,
        Json(AuthResponse {
            success: true,
            message: Some("User created successfully".to_string()),
            token: None,
        }),
    ))
}

pub async fn login(
    Extension(db_ctx): Extension<DbContext>,
    Json(body): Json<LoginRequest>,
) -> Result<impl IntoResponse, AuthRouteError> {
    let user = sqlx::query!(
        "SELECT id, password FROM users WHERE email = $1 AND auth_provider = 'local'",
        body.email
    )
    .fetch_optional(&db_ctx.pool)
    .await
    .map_err(|e| AuthRouteError::DatabaseError(e.to_string()))?;

    let user_record = match user {
        Some(u) => u,
        None => return Err(AuthRouteError::InvalidCredentials),
    };

    let matches = verify(body.password, &user_record.password)
        .map_err(|e| AuthRouteError::DatabaseError(e.to_string()))?;

    if !matches {
        return Err(AuthRouteError::InvalidCredentials);
    }

    let token = make_token(&user_record.id.to_string())
        .map_err(|e| AuthRouteError::DatabaseError(e.to_string()))?;

    Ok(Json(AuthResponse {
        success: true,
        message: None,
        token: Some(token),
    }))
}

// ──────────────────────────────────────────────
// POST /api/google-auth
// ──────────────────────────────────────────────
pub async fn google_auth(
    Extension(db_ctx): Extension<DbContext>,
    Json(body): Json<GoogleAuthRequest>,
) -> Result<impl IntoResponse, AuthRouteError> {
    let google_client_id = env::var("GOOGLE_CLIENT_ID").unwrap_or_else(|_| "".to_string());
    let google_client = GoogleClient::new(&google_client_id);

    let id_token = google_client
        .verify_id_token(&body.token)
        .map_err(|_| AuthRouteError::InvalidCredentials)?;

    // FIXED: Extracted fields directly since they return standard String data types
    let email = id_token.get_payload().get_email();
    let name = id_token.get_payload().get_name();

    let user = sqlx::query!("SELECT id FROM users WHERE email = $1", email)
        .fetch_optional(&db_ctx.pool)
        .await
        .map_err(|e| AuthRouteError::DatabaseError(e.to_string()))?;

    let user_id = match user {
        Some(existing_user) => existing_user.id.to_string(),
        None => {
            let new_user = sqlx::query!(
                "INSERT INTO users (name, email, password, auth_provider) VALUES ($1, $2, '', 'google') RETURNING id",
                name,
                email
            )
            .fetch_one(&db_ctx.pool)
            .await
            .map_err(|e| AuthRouteError::DatabaseError(e.to_string()))?;

            new_user.id.to_string()
        }
    };

    let token = make_token(&user_id).map_err(|e| AuthRouteError::DatabaseError(e))?;

    Ok(Json(AuthResponse {
        success: true,
        message: None,
        token: Some(token),
    }))
}
