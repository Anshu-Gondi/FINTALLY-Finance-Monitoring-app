//! Shared kernel state and bounded login attempt limiting.

use crate::config::KernelConfig;
use crate::observability::metrics::Metrics;
use crate::sessions::{SessionStore, SessionStoreError};
use fintally_db::DbContext;
use fintally_db_local::LocalDbContext;
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::sync::{atomic::AtomicBool, Arc, Mutex};
use std::time::{Duration, Instant};

const MAX_LOGIN_RATE_KEYS: usize = 1_024;

struct AttemptWindow {
    starts_at: Instant,
    attempts: u32,
}

/// Fixed-window limiter keyed by a digest of a normalized email address.
/// It never stores or logs raw emails and has a hard entry count.
pub struct LoginRateLimiter {
    windows: Mutex<HashMap<[u8; 32], AttemptWindow>>,
    max_attempts: u32,
    window: Duration,
}

impl LoginRateLimiter {
    pub fn new(max_attempts: u32, window: Duration) -> Self {
        Self {
            windows: Mutex::new(HashMap::new()),
            max_attempts: max_attempts.max(1),
            window: window.max(Duration::from_secs(1)),
        }
    }

    pub fn allow(&self, email: &str) -> bool {
        let normalized = email.trim().to_ascii_lowercase();
        let digest = Sha256::digest(normalized.as_bytes());
        let mut key = [0_u8; 32];
        key.copy_from_slice(&digest);
        let now = Instant::now();
        let Ok(mut windows) = self.windows.lock() else {
            // Fail closed if a poisoned mutex makes the limiter unavailable.
            return false;
        };

        windows.retain(|_, entry| now.duration_since(entry.starts_at) < self.window);
        if let Some(entry) = windows.get_mut(&key) {
            if now.duration_since(entry.starts_at) >= self.window {
                entry.starts_at = now;
                entry.attempts = 1;
                return true;
            }
            if entry.attempts >= self.max_attempts {
                return false;
            }
            entry.attempts += 1;
            return true;
        }

        if windows.len() >= MAX_LOGIN_RATE_KEYS {
            // A flood of unique email addresses cannot grow the map without
            // bound. Existing live windows continue to be rate-limited.
            return false;
        }
        windows.insert(
            key,
            AttemptWindow {
                starts_at: now,
                attempts: 1,
            },
        );
        true
    }
}

pub struct KernelState {
    /// Cloud database is optional so a local-only installation can boot with
    /// no DATABASE_URL and no connection to Supabase.
    pub db: Option<DbContext>,
    pub local_db: LocalDbContext,
    /// False until the additive CloudSync recurrence migration has succeeded.
    pub cloud_recurring_schema_ready: Arc<AtomicBool>,
    pub sessions: SessionStore,
    pub login_rate_limiter: LoginRateLimiter,
    pub metrics: Arc<Metrics>,
}

impl KernelState {
    pub fn new(
        db: Option<DbContext>,
        local_db: LocalDbContext,
        config: &KernelConfig,
        metrics: Arc<Metrics>,
    ) -> Result<Arc<Self>, SessionStoreError> {
        let sessions = SessionStore::new(config.max_sessions, config.session_ttl)?;
        Ok(Arc::new(Self {
            db,
            local_db,
            cloud_recurring_schema_ready: Arc::new(AtomicBool::new(false)),
            sessions,
            login_rate_limiter: LoginRateLimiter::new(
                config.login_attempts_per_window,
                config.login_window,
            ),
            metrics,
        }))
    }
}

#[cfg(test)]
mod tests {
    use super::LoginRateLimiter;
    use std::time::Duration;

    #[test]
    fn rate_limiter_caps_attempts_per_normalized_email() {
        let limiter = LoginRateLimiter::new(2, Duration::from_secs(60));
        assert!(limiter.allow("Person@example.test"));
        assert!(limiter.allow("person@example.test"));
        assert!(!limiter.allow("PERSON@example.test"));
        assert!(limiter.allow("other@example.test"));
    }
}
