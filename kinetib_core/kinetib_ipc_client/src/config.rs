//! Bounded timeout configuration for an IPC client.

use std::time::Duration;
use thiserror::Error;

const MAX_TIMEOUT: Duration = Duration::from_secs(300);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ClientConfig {
    connect_timeout: Duration,
    request_timeout: Duration,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum ConfigError {
    #[error("IPC timeout must be non-zero")]
    ZeroTimeout,
    #[error("IPC timeout exceeds five minutes")]
    TimeoutTooLarge,
}

impl Default for ClientConfig {
    fn default() -> Self {
        Self {
            connect_timeout: Duration::from_secs(2),
            // Login includes password verification; this is deliberately not
            // the codec's microsecond performance target.
            request_timeout: Duration::from_secs(5),
        }
    }
}

impl ClientConfig {
    pub fn with_timeouts(
        connect_timeout: Duration,
        request_timeout: Duration,
    ) -> Result<Self, ConfigError> {
        if connect_timeout.is_zero() || request_timeout.is_zero() {
            return Err(ConfigError::ZeroTimeout);
        }
        if connect_timeout > MAX_TIMEOUT || request_timeout > MAX_TIMEOUT {
            return Err(ConfigError::TimeoutTooLarge);
        }
        Ok(Self {
            connect_timeout,
            request_timeout,
        })
    }

    pub const fn connect_timeout(self) -> Duration {
        self.connect_timeout
    }

    pub const fn request_timeout(self) -> Duration {
        self.request_timeout
    }
}
