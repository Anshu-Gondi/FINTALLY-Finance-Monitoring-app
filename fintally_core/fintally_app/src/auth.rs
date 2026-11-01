use bcrypt::{hash, verify, DEFAULT_COST};
use fintally_db::DbContext;
use fintally_db_local::{LocalDbContext, LocalDbError};
use std::sync::{Arc, OnceLock};
use thiserror::Error;
use tokio::sync::{OwnedSemaphorePermit, Semaphore};

const MAX_EMAIL_BYTES: usize = 320;
/// bcrypt considers at most 72 password bytes. Reject longer input rather than
/// permitting two distinct passwords to become equivalent through truncation.
const MAX_PASSWORD_BYTES: usize = 72;

/// Bound database-backed authentication attempts and expensive bcrypt work.
/// Per-IP/account rate limiting belongs at the kernel/service boundary.
const MAX_CONCURRENT_AUTHENTICATIONS: usize = 4;
static AUTHENTICATION_PERMITS: OnceLock<Arc<Semaphore>> = OnceLock::new();

#[derive(Debug, Error, PartialEq, Eq)]
pub enum AuthError {
    #[error("invalid credentials")]
    InvalidCredentials,

    /// This error deliberately contains no SQLx source error. Do not surface
    /// database diagnostics, credentials, or hashes through the UI/API.
    #[error("database operation failed")]
    Database,

    #[error("authentication is temporarily busy")]
    Busy,

    #[error("account already exists")]
    AccountExists,

    /// This is a generic worker failure; it does not expose panic payloads.
    #[error("password verification worker failed")]
    PasswordWorker,
}

/// An authenticated user identity established by this application layer.
///
/// The private field prevents callers from constructing a principal using a
/// struct literal with an arbitrary user ID.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AuthenticatedPrincipal {
    user_id: i64,
}

impl AuthenticatedPrincipal {
    /// Returns the database identity of the authenticated user.
    pub fn user_id(&self) -> i64 {
        self.user_id
    }
}

/// Validate credentials before taking a permit or doing database/hash work.
/// Lengths are UTF-8 byte lengths, not Unicode scalar-value counts.
fn validate_credentials_input(email: &str, password: &str) -> Result<(), AuthError> {
    if email.trim().is_empty()
        || email.len() > MAX_EMAIL_BYTES
        || password.is_empty()
        || password.len() > MAX_PASSWORD_BYTES
    {
        return Err(AuthError::InvalidCredentials);
    }

    Ok(())
}

fn try_permit_from(gate: &Arc<Semaphore>) -> Result<OwnedSemaphorePermit, AuthError> {
    Arc::clone(gate)
        .try_acquire_owned()
        .map_err(|_| AuthError::Busy)
}

fn try_authentication_permit() -> Result<OwnedSemaphorePermit, AuthError> {
    let gate = AUTHENTICATION_PERMITS
        .get_or_init(|| Arc::new(Semaphore::new(MAX_CONCURRENT_AUTHENTICATIONS)));
    try_permit_from(gate)
}

/// Malformed stored hashes fail closed and have the same public result as a
/// wrong password or a nonexistent local account.
fn stored_hash_matches(password: &str, password_hash: &str) -> bool {
    match verify(password, password_hash) {
        Ok(matches) => matches,
        Err(_) => {
            // A malformed stored hash can fail before the normal bcrypt work.
            // Do dummy work on the same blocking worker, then fail closed.
            let _ = hash(password, DEFAULT_COST);
            false
        }
    }
}

async fn run_password_check<F>(permit: OwnedSemaphorePermit, check: F) -> Result<bool, AuthError>
where
    F: FnOnce() -> bool + Send + 'static,
{
    tokio::task::spawn_blocking(move || {
        // Hold the permit for the complete lifetime of the blocking operation,
        // including unwinding if the worker panics.
        let _permit = permit;
        check()
    })
    .await
    .map_err(|_| AuthError::PasswordWorker)
}

/// Run bcrypt hashing on the blocking pool while retaining the bounded permit.
async fn run_password_hash(
    permit: OwnedSemaphorePermit,
    password: String,
) -> Result<String, AuthError> {
    tokio::task::spawn_blocking(move || {
        let _permit = permit;
        hash(password, DEFAULT_COST).map_err(|_| AuthError::PasswordWorker)
    })
    .await
    .map_err(|_| AuthError::PasswordWorker)?
}

fn validate_signup_input(email: &str, password: &str) -> Result<(), AuthError> {
    validate_credentials_input(email, password)?;
    if password.len() < 8 {
        return Err(AuthError::InvalidCredentials);
    }
    Ok(())
}

fn local_unique_violation(error: &LocalDbError) -> bool {
    matches!(error, LocalDbError::Database(sqlx::Error::Database(database_error)) if database_error.is_unique_violation())
}

/// Register an email/password account in the device-local SQLite store.
/// This function has no cloud database handle and therefore cannot write to
/// Supabase by construction.
pub async fn register_password_local(
    db: &LocalDbContext,
    email: &str,
    password: &str,
) -> Result<AuthenticatedPrincipal, AuthError> {
    validate_signup_input(email, password)?;
    let permit = try_authentication_permit()?;
    let password_hash = run_password_hash(permit, password.to_owned()).await?;
    let normalized_email = email.trim().to_ascii_lowercase();
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_err(|_| AuthError::Database)?
        .as_secs();
    let created_at_unix = i64::try_from(now).map_err(|_| AuthError::Database)?;

    match db
        .create_user(&normalized_email, &password_hash, created_at_unix)
        .await
    {
        Ok(user_id) => Ok(AuthenticatedPrincipal { user_id }),
        Err(error) if local_unique_violation(&error) => Err(AuthError::AccountExists),
        Err(_) => Err(AuthError::Database),
    }
}

/// Authenticate an account from the device-local SQLite store. The cloud
/// `DbContext` is deliberately not accepted by this API.
pub async fn authenticate_password_local(
    db: &LocalDbContext,
    email: &str,
    password: &str,
) -> Result<AuthenticatedPrincipal, AuthError> {
    validate_credentials_input(email, password)?;
    let permit = try_authentication_permit()?;
    let normalized_email = email.trim().to_ascii_lowercase();
    let user = db
        .find_user(&normalized_email)
        .await
        .map_err(|_| AuthError::Database)?;

    let Some(user) = user else {
        let dummy_password = password.to_owned();
        let _ = run_password_check(permit, move || {
            let _ = hash(dummy_password, DEFAULT_COST);
            false
        })
        .await?;
        return Err(AuthError::InvalidCredentials);
    };

    let password_owned = password.to_owned();
    let password_hash = user.password_hash;
    let matches = run_password_check(permit, move || {
        stored_hash_matches(&password_owned, &password_hash)
    })
    .await?;
    if !matches {
        return Err(AuthError::InvalidCredentials);
    }
    Ok(AuthenticatedPrincipal { user_id: user.id })
}

/// Register an email/password account in the cloud PostgreSQL store after the
/// user explicitly selects CloudSync. The SQL matches the users schema used by
/// the existing kernel integration fixture and is compile-time checked.
pub async fn register_password_cloud(
    db: &DbContext,
    email: &str,
    password: &str,
) -> Result<AuthenticatedPrincipal, AuthError> {
    validate_signup_input(email, password)?;
    let permit = try_authentication_permit()?;
    let password_hash = run_password_hash(permit, password.to_owned()).await?;
    let normalized_email = email.trim().to_ascii_lowercase();

    let row = sqlx::query!(
        "INSERT INTO users (email, password, auth_provider) VALUES ($1, $2, 'local') RETURNING id",
        normalized_email,
        password_hash
    )
    .fetch_one(&db.pool)
    .await;

    match row {
        Ok(row) => Ok(AuthenticatedPrincipal { user_id: row.id }),
        Err(sqlx::Error::Database(error)) if error.code().as_deref() == Some("23505") => {
            Err(AuthError::AccountExists)
        }
        Err(_) => Err(AuthError::Database),
    }
}

/// Authenticate an account using the existing local bcrypt password hash.
///
/// This operation does not issue an IPC session token. The kernel is
/// responsible for creating a session after authentication succeeds.
///
/// A non-blocking global permit bounds concurrent database-backed auth flows
/// and password checks. Requests beyond the cap fail fast with `AuthError::Busy`.
/// Apply per-client/per-account request-rate limits in the kernel as well.
///
/// The SQL is intentionally identical to the query recorded in the existing
/// workspace SQLx offline metadata.
pub async fn authenticate_password(
    db: &DbContext,
    email: &str,
    password: &str,
) -> Result<AuthenticatedPrincipal, AuthError> {
    validate_credentials_input(email, password)?;
    let permit = try_authentication_permit()?;

    let normalized_email = email.trim().to_ascii_lowercase();
    let user = sqlx::query!(
        "SELECT id, password FROM users WHERE email = $1 AND auth_provider = 'local'",
        normalized_email
    )
    .fetch_optional(&db.pool)
    .await
    .map_err(|_| AuthError::Database)?;

    let Some(user) = user else {
        // Perform dummy bcrypt work for a missing account so the ordinary
        // missing-account path is closer to the password-verification path.
        // Keep stored hashes on a consistent work factor and retain kernel
        // rate limits; this is mitigation, not a constant-time guarantee.
        let dummy_password = password.to_owned();
        let _ = run_password_check(permit, move || {
            let _ = hash(&dummy_password, DEFAULT_COST);
            false
        })
        .await?;
        return Err(AuthError::InvalidCredentials);
    };

    let user_id = user.id;
    let password_hash = user.password;
    let password = password.to_owned();

    let matches = run_password_check(permit, move || {
        stored_hash_matches(&password, &password_hash)
    })
    .await?;

    if !matches {
        return Err(AuthError::InvalidCredentials);
    }

    Ok(AuthenticatedPrincipal { user_id })
}

#[cfg(test)]
mod tests {
    use super::{
        run_password_check, stored_hash_matches, try_permit_from, validate_credentials_input,
        AuthError, MAX_EMAIL_BYTES, MAX_PASSWORD_BYTES,
    };
    use bcrypt::hash;
    use std::sync::Arc;
    use tokio::sync::Semaphore;

    #[test]
    fn rejects_empty_email() {
        assert!(matches!(
            validate_credentials_input("", "password"),
            Err(AuthError::InvalidCredentials)
        ));
    }

    #[test]
    fn rejects_whitespace_only_email() {
        assert!(matches!(
            validate_credentials_input("   ", "password"),
            Err(AuthError::InvalidCredentials)
        ));
    }

    #[test]
    fn rejects_empty_password() {
        assert!(matches!(
            validate_credentials_input("user@example.com", ""),
            Err(AuthError::InvalidCredentials)
        ));
    }

    #[test]
    fn rejects_oversized_email_by_utf8_bytes() {
        let email = "a".repeat(MAX_EMAIL_BYTES + 1);
        assert!(matches!(
            validate_credentials_input(&email, "password"),
            Err(AuthError::InvalidCredentials)
        ));
    }

    #[test]
    fn rejects_password_above_bcrypt_72_byte_limit() {
        let password = "p".repeat(MAX_PASSWORD_BYTES + 1);
        assert!(matches!(
            validate_credentials_input("user@example.com", &password),
            Err(AuthError::InvalidCredentials)
        ));
    }

    #[test]
    fn password_length_uses_utf8_bytes_not_character_count() {
        let exactly_72_bytes = "💙".repeat(18); // 18 * 4 UTF-8 bytes
        let seventy_six_bytes = "💙".repeat(19); // 19 * 4 UTF-8 bytes

        assert!(validate_credentials_input("user@example.com", &exactly_72_bytes).is_ok());
        assert!(matches!(
            validate_credentials_input("user@example.com", &seventy_six_bytes),
            Err(AuthError::InvalidCredentials)
        ));
    }

    #[test]
    fn accepts_bounded_nonempty_credentials() {
        assert!(validate_credentials_input("user@example.com", "password").is_ok());
    }

    #[test]
    fn malformed_stored_hash_fails_closed() {
        assert!(!stored_hash_matches("some-password", "not-a-bcrypt-hash"));
    }

    #[test]
    fn wrong_password_does_not_match_a_valid_hash() {
        let password_hash = hash("correct-password", 4).expect("create test hash");
        assert!(!stored_hash_matches("wrong-password", &password_hash));
    }

    #[test]
    fn correct_password_matches_a_valid_hash() {
        let password_hash = hash("correct-password", 4).expect("create test hash");
        assert!(stored_hash_matches("correct-password", &password_hash));
    }

    #[test]
    fn worker_panic_is_mapped_to_a_generic_error() {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .build()
            .expect("create test runtime");
        let local_gate = Arc::new(Semaphore::new(1));
        let permit = local_gate
            .try_acquire_owned()
            .expect("acquire local permit");

        let result = runtime.block_on(run_password_check(permit, || {
            panic!("synthetic worker panic")
        }));
        assert!(matches!(result, Err(AuthError::PasswordWorker)));
        assert_eq!(
            AuthError::PasswordWorker.to_string(),
            "password verification worker failed"
        );
    }

    #[test]
    fn a_full_local_gate_rejects_additional_work() {
        let gate = Arc::new(Semaphore::new(1));
        let held = try_permit_from(&gate).expect("first permit");
        assert!(matches!(try_permit_from(&gate), Err(AuthError::Busy)));
        drop(held);
        assert!(try_permit_from(&gate).is_ok());
    }
}
