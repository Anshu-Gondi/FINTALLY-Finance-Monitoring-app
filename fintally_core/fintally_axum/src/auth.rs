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
use jsonwebtoken::{decode, DecodingKey, Validation, Algorithm};
use serde::{Deserialize, Serialize};
use std::sync::OnceLock;
use tracing::{error, warn};

/// 1. Strict Secret Management
/// Caches the JWT secret in memory on first use.
/// REMOVED the hardcoded "secret123" fallback - deploying with default secrets is a critical security risk.
static JWT_SECRET: OnceLock<Vec<u8>> = OnceLock::new();

fn get_jwt_secret() -> &'static [u8] {
    JWT_SECRET.get_or_init(|| {
        std::env::var("JWT_SECRET")
            .expect("🚨 FATAL SECURITY ERROR: JWT_SECRET environment variable is missing. Application refuses to start in an insecure state.")
            .into_bytes()
    })
}

/// 2. Strict Payload Definitions (Claims)
/// Defines the exact structural shape of the JWT. Added `iat` (Issued At) to monitor token age.
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Claims {
    #[serde(rename = "userId")]
    pub user_id: String,
    pub exp: u64, // Expiration timestamp (Mandatory)
    pub iat: u64, // Issued At timestamp (Mandatory for strict compliance)
}

/// 3. Standardized Security Responses
/// Prevents leaking sensitive stack traces or cryptographic hints to potential attackers.
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

impl IntoResponse for AuthError {
    fn into_response(self) -> Response {
        let (status, detail) = match self {
            AuthError::MissingToken => (StatusCode::UNAUTHORIZED, "Missing or malformed Authorization header."),
            AuthError::ExpiredToken => (StatusCode::UNAUTHORIZED, "Token has expired. Please re-authenticate."),
            // We purposefully return 401 Unauthorized (not 403) and obscure the exact crypto failure
            AuthError::InvalidToken => (StatusCode::UNAUTHORIZED, "Invalid authentication credentials."),
        };

        let body = Json(AuthErrorResponse {
            error: "Unauthorized".to_string(),
            detail: detail.to_string(),
        });

        (status, body).into_response()
    }
}

/// 4. DPDPA-Compliant High-Performance Auth Extractor
/// Acts as a secure gatekeeper. Automatically intercepts requests, validates cryptographically,
/// and logs malicious/failed access attempts for compliance auditing.
#[async_trait]
impl<S> FromRequestParts<S> for Claims
where
    S: Send + Sync,
{
    type Rejection = AuthError;

    async fn from_request_parts(parts: &mut Parts, state: &S) -> Result<Self, Self::Rejection> {
        let uri_path = parts.uri.path().to_string();

        // FIXED E0502: Add .to_string() here so we own the string and stop borrowing `parts`
        let client_ip = parts
            .headers
            .get("x-forwarded-for")
            .and_then(|h| h.to_str().ok())
            .unwrap_or("direct/unknown")
            .to_string();

        // Step A: Extract the Bearer Token
        let TypedHeader(Authorization(bearer)) = TypedHeader::<Authorization<Bearer>>::from_request_parts(parts, state)
            .await
            .map_err(|_| {
                warn!(
                    target: "dpdpa_auth_audit",
                    "🔒 [AUTH ALERT] Missing or malformed token | Path: {} | IP: {}",
                    uri_path, client_ip
                );
                AuthError::MissingToken
            })?;

        // Step B: Configure Strict Cryptographic Validation
        let secret = get_jwt_secret();
        let decoding_key = DecodingKey::from_secret(secret);

        let mut validation = Validation::new(Algorithm::HS256);
        validation.leeway = 0;
        validation.validate_exp = true;
        validation.set_required_spec_claims(&["exp", "iat", "userId"]);

        // Step C: Decode and Validate
        let token_data = decode::<Claims>(bearer.token(), &decoding_key, &validation)
            .map_err(|err| match err.kind() {
                jsonwebtoken::errors::ErrorKind::ExpiredSignature => {
                    warn!(
                        target: "dpdpa_auth_audit",
                        "🕒 [AUTH ALERT] Expired token attempt | Path: {} | IP: {}",
                        uri_path, client_ip
                    );
                    AuthError::ExpiredToken
                }
                _ => {
                    error!(
                        target: "dpdpa_auth_audit",
                        "🚨 [SECURITY VIOLATION] Forged/Invalid token rejected! Reason: {:?} | Path: {} | IP: {}",
                        err.kind(), uri_path, client_ip
                    );
                    AuthError::InvalidToken
                }
            })?;

        Ok(token_data.claims)
    }
}
