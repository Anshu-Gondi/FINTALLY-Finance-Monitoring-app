//! Authentication and data-residency DTOs for the FinTally IPC protocol.
//!
//! These are wire contracts only. They do not authenticate users or select a
//! database. The kernel/application must enforce `StorageMode` on every request.

use serde::{Deserialize, Serialize};
use std::fmt;

/// Explicit data-residency choice. Never infer CloudSync when the choice is absent.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum StorageMode {
    /// User records and finance data remain in the device-local store.
    /// The FinTally kernel must not route this identity to Supabase.
    LocalOnly,
    /// The user has explicitly opted into a cloud-backed account/data flow.
    CloudSync,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum IdentityProvider {
    EmailPassword,
    Google,
}

/// Legacy response retained for the existing `Command::Login` contract.
/// New flows should use `AuthSession` so the storage mode/provider are explicit.
#[derive(Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct LoginResult {
    pub session_token: String,
}

impl fmt::Debug for LoginResult {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("LoginResult")
            .field("session_token", &"[REDACTED]")
            .finish()
    }
}

/// Successful response to signup, explicit-mode email login, or Google sign-in.
/// The bearer token must never be logged or persisted in UI preferences.
#[derive(Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct AuthSession {
    pub session_token: String,
    pub storage_mode: StorageMode,
    pub provider: IdentityProvider,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub expires_at_unix_seconds: Option<u64>,
}

impl fmt::Debug for AuthSession {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("AuthSession")
            .field("session_token", &"[REDACTED]")
            .field("storage_mode", &self.storage_mode)
            .field("provider", &self.provider)
            .field("expires_at_unix_seconds", &self.expires_at_unix_seconds)
            .finish()
    }
}

/// Server-confirmed session status. A client-side cached token alone is not proof
/// that the session remains valid.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct SessionStatus {
    pub authenticated: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub storage_mode: Option<StorageMode>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub provider: Option<IdentityProvider>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub expires_at_unix_seconds: Option<u64>,
}

impl SessionStatus {
    /// Reject contradictory status payloads instead of letting UI state drift.
    pub fn is_well_formed(&self) -> bool {
        if self.authenticated {
            self.storage_mode.is_some() && self.provider.is_some()
        } else {
            self.storage_mode.is_none()
                && self.provider.is_none()
                && self.expires_at_unix_seconds.is_none()
        }
    }
}

/// `revoked` is false when the session was already expired/revoked. The client
/// must still erase its local bearer token after a successful logout response.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct LogoutResult {
    pub revoked: bool,
}

#[cfg(test)]
mod tests {
    use super::{AuthSession, IdentityProvider, LoginResult, SessionStatus, StorageMode};

    #[test]
    fn token_debug_output_is_redacted() {
        let legacy = LoginResult {
            session_token: "legacy-secret".into(),
        };
        let session = AuthSession {
            session_token: "bearer-secret".into(),
            storage_mode: StorageMode::LocalOnly,
            provider: IdentityProvider::EmailPassword,
            expires_at_unix_seconds: Some(42),
        };

        assert!(!format!("{legacy:?}").contains("legacy-secret"));
        let debug = format!("{session:?}");
        assert!(debug.contains("[REDACTED]"));
        assert!(!debug.contains("bearer-secret"));
    }

    #[test]
    fn session_status_rejects_contradictory_shapes() {
        assert!(SessionStatus {
            authenticated: true,
            storage_mode: Some(StorageMode::LocalOnly),
            provider: Some(IdentityProvider::EmailPassword),
            expires_at_unix_seconds: Some(42),
        }
        .is_well_formed());

        assert!(SessionStatus {
            authenticated: false,
            storage_mode: None,
            provider: None,
            expires_at_unix_seconds: None,
        }
        .is_well_formed());

        assert!(!SessionStatus {
            authenticated: true,
            storage_mode: None,
            provider: None,
            expires_at_unix_seconds: None,
        }
        .is_well_formed());
    }

    #[test]
    fn storage_mode_wire_names_are_explicit() {
        assert_eq!(
            serde_json::to_string(&StorageMode::LocalOnly).unwrap(),
            "\"local_only\""
        );
        assert_eq!(
            serde_json::to_string(&StorageMode::CloudSync).unwrap(),
            "\"cloud_sync\""
        );
    }
}
