//! Authenticated feedback command policy and bounded per-user rate limiting.

use crate::sessions::SessionIdentity;
use crate::state::KernelState;
use kinetib_app::feedback::{FeedbackService, FeedbackServiceError};
use kinetib_ipc_protocol::{ErrorCode, FeedbackReceipt, FeedbackSubmissionRequest, StorageMode};
use std::collections::HashMap;
use std::sync::Mutex;
use std::time::{Duration, Instant};

const MAX_FEEDBACK_RATE_KEYS: usize = 256;
const MAX_FEEDBACK_SUBMISSIONS: u32 = 5;
const FEEDBACK_WINDOW: Duration = Duration::from_secs(15 * 60);

#[derive(Clone, Copy)]
struct AttemptWindow {
    starts_at: Instant,
    attempts: u32,
}

/// Bounded fixed-window limiter keyed by the trusted CloudSync principal ID.
pub struct FeedbackRateLimiter {
    windows: Mutex<HashMap<i64, AttemptWindow>>,
}

impl Default for FeedbackRateLimiter {
    fn default() -> Self {
        Self {
            windows: Mutex::new(HashMap::new()),
        }
    }
}

impl FeedbackRateLimiter {
    pub fn allow(&self, user_id: i64) -> bool {
        if user_id <= 0 {
            return false;
        }
        let now = Instant::now();
        let Ok(mut windows) = self.windows.lock() else {
            return false;
        };
        windows.retain(|_, window| now.duration_since(window.starts_at) < FEEDBACK_WINDOW);
        if let Some(window) = windows.get_mut(&user_id) {
            if window.attempts >= MAX_FEEDBACK_SUBMISSIONS {
                return false;
            }
            window.attempts += 1;
            return true;
        }
        if windows.len() >= MAX_FEEDBACK_RATE_KEYS || windows.try_reserve(1).is_err() {
            return false;
        }
        windows.insert(
            user_id,
            AttemptWindow {
                starts_at: now,
                attempts: 1,
            },
        );
        true
    }
}

pub async fn submit(
    state: &KernelState,
    identity: &SessionIdentity,
    submission: &FeedbackSubmissionRequest,
) -> Result<FeedbackReceipt, ErrorCode> {
    if identity.storage_mode != StorageMode::CloudSync {
        return Err(ErrorCode::Forbidden);
    }
    if !state
        .cloud_recurring_schema_ready
        .load(std::sync::atomic::Ordering::Acquire)
    {
        return Err(ErrorCode::Busy);
    }
    let db = state.db.as_ref().ok_or(ErrorCode::Busy)?;
    if !state
        .feedback_rate_limiter
        .allow(identity.principal.user_id())
    {
        return Err(ErrorCode::Busy);
    }

    FeedbackService::new(db)
        .submit(&identity.principal, submission)
        .await
        .map_err(map_service_error)
}

fn map_service_error(error: FeedbackServiceError) -> ErrorCode {
    match error {
        FeedbackServiceError::Validation(_) => ErrorCode::InvalidRequest,
        FeedbackServiceError::Persistence(error) => match error {
            kinetib_db::feedback::FeedbackRepositoryError::IdempotencyConflict => {
                ErrorCode::Conflict
            }
            kinetib_db::feedback::FeedbackRepositoryError::InvalidOwner => ErrorCode::Forbidden,
            kinetib_db::feedback::FeedbackRepositoryError::Database(_)
            | kinetib_db::feedback::FeedbackRepositoryError::Serialization => ErrorCode::Internal,
        },
    }
}

#[cfg(test)]
mod tests {
    use super::FeedbackRateLimiter;

    #[test]
    fn rate_limit_is_scoped_to_owner_and_bounded() {
        let limiter = FeedbackRateLimiter::default();
        for _ in 0..5 {
            assert!(limiter.allow(7));
        }
        assert!(!limiter.allow(7));
        assert!(limiter.allow(8));
        assert!(!limiter.allow(0));
    }
}
