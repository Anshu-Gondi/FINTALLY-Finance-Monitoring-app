//! Authentication-specific protocol bounds. All limits are UTF-8 bytes.

/// Minimum signup password length; login also permits legacy shorter passwords.
pub const MIN_SIGNUP_PASSWORD_BYTES: usize = 8;

/// Share the canonical password limit so validation and public protocol constants cannot drift.
pub const MAX_AUTH_PASSWORD_BYTES: usize = crate::limits::MAX_PASSWORD_BYTES;

/// Compact Google ID tokens are bounded before decode or signature verification.
pub const MAX_GOOGLE_ID_TOKEN_BYTES: usize = 8 * 1024;
