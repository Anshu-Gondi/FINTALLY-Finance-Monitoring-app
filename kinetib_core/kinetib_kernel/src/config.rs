//! Bounded runtime configuration for the local IPC kernel.

use std::env;
use std::fmt;
use std::path::PathBuf;
use std::time::Duration;
use thiserror::Error;

const DEFAULT_SESSION_TTL_SECS: u64 = 15 * 60;
const MAX_SESSION_TTL_SECS: u64 = 24 * 60 * 60;
const DEFAULT_MAX_SESSIONS: usize = 128;
const HARD_MAX_SESSIONS: usize = 256;
const DEFAULT_MAX_CONNECTIONS: usize = 8;
const HARD_MAX_CONNECTIONS: usize = 32;
const DEFAULT_DB_MAX_CONNECTIONS: u32 = 8;
const HARD_MAX_DB_CONNECTIONS: u32 = 32;
const DEFAULT_LOGIN_ATTEMPTS: u32 = 5;
const DEFAULT_LOGIN_WINDOW_SECS: u64 = 60;

#[derive(Clone)]
pub struct KernelConfig {
    pub database_url: Option<String>,
    pub local_db_path: PathBuf,
    pub socket_path: PathBuf,
    pub session_ttl: Duration,
    pub max_sessions: usize,
    pub max_connections: usize,
    pub db_max_connections: u32,
    pub login_attempts_per_window: u32,
    pub login_window: Duration,
}

impl fmt::Debug for KernelConfig {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("KernelConfig")
            .field(
                "database_url",
                &self.database_url.as_ref().map(|_| "[REDACTED]"),
            )
            .field("local_db_path", &self.local_db_path)
            .field("socket_path", &self.socket_path)
            .field("session_ttl", &self.session_ttl)
            .field("max_sessions", &self.max_sessions)
            .field("max_connections", &self.max_connections)
            .field("db_max_connections", &self.db_max_connections)
            .field("login_attempts_per_window", &self.login_attempts_per_window)
            .field("login_window", &self.login_window)
            .finish()
    }
}

#[derive(Debug, Error)]
pub enum ConfigError {
    #[error("invalid or out-of-range environment variable: {0}")]
    InvalidValue(&'static str),
}

impl KernelConfig {
    pub fn from_env() -> Result<Self, ConfigError> {
        // Load the workspace .env when present; explicit process environment
        // variables retain dotenvy's normal precedence.
        let _ = dotenvy::dotenv();

        // Missing DATABASE_URL is valid for a local-only installation. Cloud
        // commands fail closed at dispatch when no cloud pool is configured.
        let database_url = env::var("DATABASE_URL")
            .ok()
            .filter(|value| !value.trim().is_empty());
        let local_db_path = env::var_os("KINETIB_LOCAL_DB_PATH")
            .map(PathBuf::from)
            .unwrap_or_else(default_local_db_path);

        let socket_path = env::var_os("KINETIB_IPC_SOCKET_PATH")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("/tmp/kinetib-kernel.sock"));

        let session_ttl_secs = parse_or("KINETIB_SESSION_TTL_SECS", DEFAULT_SESSION_TTL_SECS)?;
        if !(1..=MAX_SESSION_TTL_SECS).contains(&session_ttl_secs) {
            return Err(ConfigError::InvalidValue("KINETIB_SESSION_TTL_SECS"));
        }

        let max_sessions = parse_or("KINETIB_MAX_SESSIONS", DEFAULT_MAX_SESSIONS)?;
        if !(1..=HARD_MAX_SESSIONS).contains(&max_sessions) {
            return Err(ConfigError::InvalidValue("KINETIB_MAX_SESSIONS"));
        }

        let max_connections = parse_or("KINETIB_MAX_CONNECTIONS", DEFAULT_MAX_CONNECTIONS)?;
        if !(1..=HARD_MAX_CONNECTIONS).contains(&max_connections) {
            return Err(ConfigError::InvalidValue("KINETIB_MAX_CONNECTIONS"));
        }

        let db_max_connections =
            parse_or("KINETIB_DB_MAX_CONNECTIONS", DEFAULT_DB_MAX_CONNECTIONS)?;
        if !(1..=HARD_MAX_DB_CONNECTIONS).contains(&db_max_connections) {
            return Err(ConfigError::InvalidValue("KINETIB_DB_MAX_CONNECTIONS"));
        }

        let login_attempts_per_window =
            parse_or("KINETIB_LOGIN_ATTEMPTS_PER_WINDOW", DEFAULT_LOGIN_ATTEMPTS)?;
        if !(1..=100).contains(&login_attempts_per_window) {
            return Err(ConfigError::InvalidValue(
                "KINETIB_LOGIN_ATTEMPTS_PER_WINDOW",
            ));
        }

        let login_window_secs = parse_or("KINETIB_LOGIN_WINDOW_SECS", DEFAULT_LOGIN_WINDOW_SECS)?;
        if !(1..=3_600).contains(&login_window_secs) {
            return Err(ConfigError::InvalidValue("KINETIB_LOGIN_WINDOW_SECS"));
        }

        Ok(Self {
            database_url,
            local_db_path,
            socket_path,
            session_ttl: Duration::from_secs(session_ttl_secs),
            max_sessions,
            max_connections,
            db_max_connections,
            login_attempts_per_window,
            login_window: Duration::from_secs(login_window_secs),
        })
    }
}

fn default_local_db_path() -> PathBuf {
    if cfg!(target_os = "windows") {
        if let Some(app_data) = env::var_os("APPDATA") {
            return PathBuf::from(app_data)
                .join("FinTally")
                .join("local.sqlite3");
        }
    }
    if let Some(home) = env::var_os("HOME") {
        let home = PathBuf::from(home);
        if cfg!(target_os = "macos") {
            return home
                .join("Library")
                .join("Application Support")
                .join("FinTally")
                .join("local.sqlite3");
        }
        // Android/iOS hosts should set KINETIB_LOCAL_DB_PATH to their private
        // application container; HOME is the fallback for sandboxed runtimes.
        return home
            .join(".local")
            .join("share")
            .join("kinetib")
            .join("local.sqlite3");
    }
    PathBuf::from("kinetib-local.sqlite3")
}

fn parse_or<T>(name: &'static str, default: T) -> Result<T, ConfigError>
where
    T: std::str::FromStr,
{
    match env::var(name) {
        Ok(value) => value.parse().map_err(|_| ConfigError::InvalidValue(name)),
        Err(env::VarError::NotPresent) => Ok(default),
        Err(env::VarError::NotUnicode(_)) => Err(ConfigError::InvalidValue(name)),
    }
}

#[cfg(test)]
mod tests {
    use super::KernelConfig;

    #[test]
    fn debug_output_redacts_database_url() {
        let config = KernelConfig {
            database_url: Some("postgres://user:secret@localhost/db".to_owned()),
            local_db_path: "/tmp/kinetib-local.sqlite3".into(),
            socket_path: "/tmp/test.sock".into(),
            session_ttl: std::time::Duration::from_secs(60),
            max_sessions: 1,
            max_connections: 1,
            db_max_connections: 1,
            login_attempts_per_window: 5,
            login_window: std::time::Duration::from_secs(60),
        };
        let debug = format!("{config:?}");
        assert!(!debug.contains("secret"));
        assert!(debug.contains("[REDACTED]"));
    }
}
