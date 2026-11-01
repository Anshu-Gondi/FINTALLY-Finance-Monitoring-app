//! Bounded in-memory session store.
//!
//! Only SHA-256 token fingerprints are used as map keys; raw bearer tokens are
//! returned once to the caller and are not retained in the store.

use super::expiry::is_expired;
use super::token::{fingerprint_token, generate_token, TokenFingerprint, TokenGenerationError};
use fintally_app::auth::AuthenticatedPrincipal;
use fintally_ipc_protocol::{IdentityProvider, StorageMode};
use std::collections::HashMap;
use std::time::{Duration, Instant};
use std::time::{SystemTime, UNIX_EPOCH};
use thiserror::Error;
use tokio::sync::RwLock;

pub const HARD_MAX_ACTIVE_SESSIONS: usize = 256;

struct Session {
    principal: AuthenticatedPrincipal,
    storage_mode: StorageMode,
    provider: IdentityProvider,
    expires_at: Instant,
    expires_at_unix_seconds: u64,
}

#[derive(Debug, Clone)]
pub struct SessionIdentity {
    pub principal: AuthenticatedPrincipal,
    pub storage_mode: StorageMode,
    pub provider: IdentityProvider,
    pub expires_at_unix_seconds: u64,
}

#[derive(Debug, Error)]
pub enum SessionStoreError {
    #[error("session capacity reached")]
    AtCapacity,
    #[error("session token generation failed")]
    TokenGeneration(#[from] TokenGenerationError),
    #[error("session store configuration is invalid")]
    InvalidConfiguration,
    #[error("session store allocation failed")]
    AllocationFailed,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionLookupError {
    InvalidOrExpired,
}

pub struct SessionStore {
    entries: RwLock<HashMap<TokenFingerprint, Session>>,
    ttl: Duration,
    max_sessions: usize,
}

impl SessionStore {
    pub fn new(max_sessions: usize, ttl: Duration) -> Result<Self, SessionStoreError> {
        if max_sessions == 0 || max_sessions > HARD_MAX_ACTIVE_SESSIONS || ttl.is_zero() {
            return Err(SessionStoreError::InvalidConfiguration);
        }
        Ok(Self {
            entries: RwLock::new(HashMap::new()),
            ttl,
            max_sessions,
        })
    }

    pub fn ttl_seconds(&self) -> u64 {
        u64::try_from(self.ttl.as_secs()).unwrap_or(u64::MAX)
    }

    /// Legacy session issuance defaults to the old cloud-backed email login.
    /// New authentication commands must call `issue_scoped` explicitly.
    pub async fn issue(
        &self,
        principal: AuthenticatedPrincipal,
    ) -> Result<String, SessionStoreError> {
        self.issue_scoped(
            principal,
            StorageMode::CloudSync,
            IdentityProvider::EmailPassword,
        )
        .await
    }

    /// Create a session with server-enforced storage scope and identity provider.
    pub async fn issue_scoped(
        &self,
        principal: AuthenticatedPrincipal,
        storage_mode: StorageMode,
        provider: IdentityProvider,
    ) -> Result<String, SessionStoreError> {
        let ttl_seconds = u64::try_from(self.ttl.as_secs()).unwrap_or(u64::MAX);
        let expires_at_unix_seconds = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs()
            .saturating_add(ttl_seconds);
        for _ in 0..3 {
            let generated = generate_token()?;
            let mut entries = self.entries.write().await;
            let now = Instant::now();
            entries.retain(|_, session| !is_expired(now, session.expires_at));

            if entries.len() >= self.max_sessions {
                return Err(SessionStoreError::AtCapacity);
            }
            if entries.contains_key(&generated.fingerprint) {
                continue;
            }
            entries
                .try_reserve(1)
                .map_err(|_| SessionStoreError::AllocationFailed)?;
            entries.insert(
                generated.fingerprint,
                Session {
                    principal: principal.clone(),
                    storage_mode,
                    provider,
                    expires_at: now + self.ttl,
                    expires_at_unix_seconds,
                },
            );
            return Ok(generated.token);
        }
        Err(SessionStoreError::TokenGeneration(TokenGenerationError))
    }

    /// Validate a bearer token and return the principal bound to that session.
    pub async fn authenticate(
        &self,
        token: &str,
    ) -> Result<AuthenticatedPrincipal, SessionLookupError> {
        self.authenticate_scoped(token)
            .await
            .map(|identity| identity.principal)
    }

    /// Validate a bearer token and return the server-owned storage scope/provider.
    pub async fn authenticate_scoped(
        &self,
        token: &str,
    ) -> Result<SessionIdentity, SessionLookupError> {
        let fingerprint = fingerprint_token(token).ok_or(SessionLookupError::InvalidOrExpired)?;
        let now = Instant::now();
        {
            let entries = self.entries.read().await;
            if let Some(session) = entries.get(&fingerprint) {
                if !is_expired(now, session.expires_at) {
                    return Ok(SessionIdentity {
                        principal: session.principal.clone(),
                        storage_mode: session.storage_mode,
                        provider: session.provider,
                        expires_at_unix_seconds: session.expires_at_unix_seconds,
                    });
                }
            } else {
                return Err(SessionLookupError::InvalidOrExpired);
            }
        }

        // Remove the expired entry with a short write lock after releasing the
        // read lock. A concurrent revoke/expiry cleanup is harmless.
        let mut entries = self.entries.write().await;
        if entries
            .get(&fingerprint)
            .is_some_and(|session| is_expired(Instant::now(), session.expires_at))
        {
            entries.remove(&fingerprint);
        }
        Err(SessionLookupError::InvalidOrExpired)
    }

    /// Revoke a token immediately. Invalid token text does not touch the map.
    pub async fn revoke(&self, token: &str) -> bool {
        let Some(fingerprint) = fingerprint_token(token) else {
            return false;
        };
        self.entries.write().await.remove(&fingerprint).is_some()
    }

    /// Remove expired entries. Returns the number of entries removed.
    pub async fn purge_expired(&self) -> usize {
        let now = Instant::now();
        let mut entries = self.entries.write().await;
        let before = entries.len();
        entries.retain(|_, session| !is_expired(now, session.expires_at));
        before - entries.len()
    }

    pub async fn len(&self) -> usize {
        self.entries.read().await.len()
    }

    pub async fn is_empty(&self) -> bool {
        self.entries.read().await.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::{SessionLookupError, SessionStore, SessionStoreError};
    use std::time::Duration;

    #[tokio::test]
    async fn rejects_tokens_with_bad_shape_without_touching_store() {
        let store = SessionStore::new(4, Duration::from_secs(60)).unwrap();
        assert_eq!(
            store.authenticate("not-a-token").await,
            Err(SessionLookupError::InvalidOrExpired)
        );
        assert!(!store.revoke("not-a-token").await);
        assert!(store.is_empty().await);
    }

    #[test]
    fn enforces_hard_session_count_bound_and_nonzero_ttl() {
        assert!(matches!(
            SessionStore::new(0, Duration::from_secs(60)),
            Err(SessionStoreError::InvalidConfiguration)
        ));
        assert!(matches!(
            SessionStore::new(1, Duration::ZERO),
            Err(SessionStoreError::InvalidConfiguration)
        ));
        assert!(matches!(
            SessionStore::new(super::HARD_MAX_ACTIVE_SESSIONS + 1, Duration::from_secs(60)),
            Err(SessionStoreError::InvalidConfiguration)
        ));
    }
}
