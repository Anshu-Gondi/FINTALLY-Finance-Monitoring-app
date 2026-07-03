use axum::{
    async_trait,
    extract::FromRequestParts,
    http::{request::Parts, StatusCode},
    response::{IntoResponse, Response},
    Json,
};
use axum_extra::{
    headers::{authorization::Bearer, Authorization},
    TypedHeader,
};
use jsonwebtoken::{decode, DecodingKey, Validation};
use serde::{Deserialize, Serialize};
use std::env;

const DEFAULT_JWT_SECRET: &str = "secret123";

// 1. Define the structural shape of your Python/Node shared JWT payload
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Claims {
    #[serde(rename = "userId")] // Maps perfectly to your original Python key "userId"
    pub user_id: String,
    pub exp: u64,               // Expiration check is verified automatically by jsonwebtoken crate
}

// 2. Define clear, descriptive API Error payloads for security transparency
#[derive(Serialize)]
struct AuthErrorResponse {
    error: String,
    detail: String,
}

pub enum AuthError {
    MissingToken,
    InvalidToken,
    ExpiredToken,
}

// Map internal structural variant states directly to strict HTTP Client codes
impl IntoResponse for AuthError {
    fn into_response(self) -> Response {
        let (status, detail) = match self {
            AuthError::MissingToken => (StatusCode::UNAUTHORIZED, "Missing or malformed Authorization header."),
            AuthError::ExpiredToken => (StatusCode::FORBIDDEN, "Token has expired."),
            AuthError::InvalidToken => (StatusCode::FORBIDDEN, "Invalid signature or token payload parsing error."),
        };

        let body = Json(AuthErrorResponse {
            error: "Unauthorized".to_string(),
            detail: detail.to_string(),
        });

        (status, body).into_response()
    }
}

// 3. Make Claims act as a high-performance, automatic Axum Extractor gate
#[async_trait]
impl<S> FromRequestParts<S> for Claims
where
    S: Send + Sync,
{
    type Rejection = AuthError;

    async fn from_request_parts(parts: &mut Parts, state: &S) -> Result<Self, Self::Rejection> {
        // Extract the raw typed Bearer authorization header seamlessly using Axum extra-headers
        let TypedHeader(Authorization(bearer)) = TypedHeader::<Authorization<Bearer>>::from_request_parts(parts, state)
            .await
            .map_err(|_| AuthError::MissingToken)?;

        // Fetch secret securely from environment variable matching your legacy system
        let secret = env::var("JWT_SECRET").unwrap_or_else(|_| DEFAULT_JWT_SECRET.to_string());
        
        // Define decoding keys and default validation checks (verifies expiration date dynamically)
        let decoding_key = DecodingKey::from_secret(secret.as_bytes());
        let validation = Validation::new(jsonwebtoken::Algorithm::HS256);

        // Parse token string
        let token_data = decode::<Claims>(bearer.token(), &decoding_key, &validation)
            .map_err(|err| match err.kind() {
                jsonwebtoken::errors::ErrorKind::ExpiredSignature => AuthError::ExpiredToken,
                _ => AuthError::InvalidToken,
            })?;

        Ok(token_data.claims)
    }
}