//! Monotonic expiry helpers; wall-clock changes do not extend sessions.

use std::time::Instant;

#[inline]
pub fn is_expired(now: Instant, expires_at: Instant) -> bool {
    now >= expires_at
}

#[cfg(test)]
mod tests {
    use super::is_expired;
    use std::time::{Duration, Instant};

    #[test]
    fn expiry_boundary_is_inclusive() {
        let now = Instant::now();
        assert!(!is_expired(now, now + Duration::from_secs(1)));
        assert!(is_expired(now, now));
    }
}
