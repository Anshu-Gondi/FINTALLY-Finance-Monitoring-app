use std::time::{SystemTime, UNIX_EPOCH};

/// Simple log levels
#[derive(Debug)]
pub enum LogLevel {
    Info,
    Warn,
    Error,
}

/// Internal helper to get a timestamp
fn timestamp() -> String {
    let start = SystemTime::now();
    let since_epoch = start.duration_since(UNIX_EPOCH).unwrap_or_default();
    let seconds = since_epoch.as_secs();
    let millis = since_epoch.subsec_millis();
    format!("{}.{:03}", seconds, millis)
}

/// Log a message
pub fn log(level: LogLevel, message: &str) {
    let level_str = match level {
        LogLevel::Info => "INFO",
        LogLevel::Warn => "WARN",
        LogLevel::Error => "ERROR",
    };

    println!("[{}] [{}] {}", timestamp(), level_str, message);
}

/// Convenience macros
#[macro_export]
macro_rules! info {
    ($msg:expr) => {
        $crate::core::utils::logging::log($crate::core::utils::logging::LogLevel::Info, $msg)
    };
    ($fmt:expr, $($arg:tt)*) => {
        $crate::core::utils::logging::log(
            $crate::core::utils::logging::LogLevel::Info,
            &format!($fmt, $($arg)*)
        )
    };
}

#[macro_export]
macro_rules! warn {
    ($msg:expr) => {
        $crate::core::utils::logging::log($crate::core::utils::logging::LogLevel::Warn, $msg)
    };
    ($fmt:expr, $($arg:tt)*) => {
        $crate::core::utils::logging::log(
            $crate::core::utils::logging::LogLevel::Warn,
            &format!($fmt, $($arg)*)
        )
    };
}

#[macro_export]
macro_rules! error {
    ($msg:expr) => {
        $crate::core::utils::logging::log($crate::core::utils::logging::LogLevel::Error, $msg)
    };
    ($fmt:expr, $($arg:tt)*) => {
        $crate::core::utils::logging::log(
            $crate::core::utils::logging::LogLevel::Error,
            &format!($fmt, $($arg)*)
        )
    };
}
