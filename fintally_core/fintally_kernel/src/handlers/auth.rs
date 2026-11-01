//! Authentication orchestration: explicit storage scope, rate limits and sessions.

use crate::state::KernelState;
use fintally_app::auth::{
    authenticate_password, authenticate_password_local, register_password_cloud,
    register_password_local, AuthError, AuthenticatedPrincipal,
};
use fintally_ipc_protocol::{AuthSession, ErrorCode, IdentityProvider, LoginResult, StorageMode};
use std::time::{SystemTime, UNIX_EPOCH};
use zeroize::Zeroize;

fn map_auth_error(error: AuthError) -> ErrorCode {
    match error {
        AuthError::InvalidCredentials => ErrorCode::Unauthenticated,
        AuthError::AccountExists => ErrorCode::Conflict,
        AuthError::Busy => ErrorCode::Busy,
        AuthError::Database | AuthError::PasswordWorker => ErrorCode::Internal,
    }
}

fn session_expiry(state: &KernelState) -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
        .saturating_add(state.sessions.ttl_seconds())
}

async fn issue_scoped_session(
    state: &KernelState,
    principal: AuthenticatedPrincipal,
    storage_mode: StorageMode,
    provider: IdentityProvider,
) -> Result<AuthSession, ErrorCode> {
    let token = state
        .sessions
        .issue_scoped(principal, storage_mode, provider)
        .await
        .map_err(|error| {
            tracing::warn!(error = %error, "unable to issue IPC session");
            match error {
                crate::sessions::SessionStoreError::AtCapacity => ErrorCode::Busy,
                crate::sessions::SessionStoreError::InvalidConfiguration
                | crate::sessions::SessionStoreError::TokenGeneration(_)
                | crate::sessions::SessionStoreError::AllocationFailed => ErrorCode::Internal,
            }
        })?;

    Ok(AuthSession {
        session_token: token,
        storage_mode,
        provider,
        expires_at_unix_seconds: Some(session_expiry(state)),
    })
}

/// Backwards-compatible login is explicitly cloud-scoped. It never silently
/// changes to a local account when cloud storage is unavailable.
pub async fn handle_legacy_login(
    state: &KernelState,
    email: &str,
    password: &str,
) -> Result<LoginResult, ErrorCode> {
    if !state.login_rate_limiter.allow(email) {
        state.metrics.authentication_failed();
        return Err(ErrorCode::Busy);
    }
    let db = state.db.as_ref().ok_or(ErrorCode::Forbidden)?;
    let principal = authenticate_password(db, email, password)
        .await
        .map_err(|error| {
            state.metrics.authentication_failed();
            map_auth_error(error)
        })?;
    let session = issue_scoped_session(
        state,
        principal,
        StorageMode::CloudSync,
        IdentityProvider::EmailPassword,
    )
    .await?;
    Ok(LoginResult {
        session_token: session.session_token,
    })
}

pub async fn handle_signup(
    state: &KernelState,
    email: &str,
    password: &str,
    storage_mode: StorageMode,
) -> Result<AuthSession, ErrorCode> {
    if !state.login_rate_limiter.allow(email) {
        state.metrics.authentication_failed();
        return Err(ErrorCode::Busy);
    }
    let principal = match storage_mode {
        StorageMode::LocalOnly => register_password_local(&state.local_db, email, password).await,
        StorageMode::CloudSync => {
            let db = state.db.as_ref().ok_or(ErrorCode::Forbidden)?;
            register_password_cloud(db, email, password).await
        }
    }
    .map_err(|error| {
        state.metrics.authentication_failed();
        map_auth_error(error)
    })?;

    issue_scoped_session(
        state,
        principal,
        storage_mode,
        IdentityProvider::EmailPassword,
    )
    .await
}

pub async fn handle_login_with_storage(
    state: &KernelState,
    email: &str,
    password: &str,
    storage_mode: StorageMode,
) -> Result<AuthSession, ErrorCode> {
    if !state.login_rate_limiter.allow(email) {
        state.metrics.authentication_failed();
        return Err(ErrorCode::Busy);
    }
    let principal = match storage_mode {
        StorageMode::LocalOnly => {
            authenticate_password_local(&state.local_db, email, password).await
        }
        StorageMode::CloudSync => {
            let db = state.db.as_ref().ok_or(ErrorCode::Forbidden)?;
            authenticate_password(db, email, password).await
        }
    }
    .map_err(|error| {
        state.metrics.authentication_failed();
        map_auth_error(error)
    })?;

    issue_scoped_session(
        state,
        principal,
        storage_mode,
        IdentityProvider::EmailPassword,
    )
    .await
}

/// Google commands intentionally fail closed until a real OIDC verifier is
/// configured. A syntactically valid JWT is not proof of identity; never accept
/// claims decoded without signature/issuer/audience/expiry verification.
pub fn reject_unverified_google_token(mut id_token: String) -> ErrorCode {
    id_token.zeroize();
    ErrorCode::Forbidden
}
