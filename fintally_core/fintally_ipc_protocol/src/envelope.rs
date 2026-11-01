//! Versioned IPC envelopes.
//!
//! Envelopes contain transport metadata and protocol payloads only. They do
//! not know about sockets, databases, UI frameworks, or application services.

use serde::{Deserialize, Serialize};
use std::fmt;

use crate::command::Command;
use crate::error::IpcError;
use crate::response::ResponsePayload;

/// A request sent by a client to the IPC service.
///
/// `session_token` is omitted from serialized login requests when absent.
/// A non-empty token is not proof of authentication; the service must verify
/// it against its session store before executing protected operations.
#[derive(Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct RequestEnvelope {
    pub protocol_version: u16,
    pub request_id: u64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub session_token: Option<String>,
    pub command: Command,
}

impl fmt::Debug for RequestEnvelope {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let session_token = self.session_token.as_ref().map(|_| "[REDACTED]");
        formatter
            .debug_struct("RequestEnvelope")
            .field("protocol_version", &self.protocol_version)
            .field("request_id", &self.request_id)
            .field("session_token", &session_token)
            .field("command", &self.command)
            .finish()
    }
}

/// A response correlated to a request by `request_id`.
///
/// Errors are stable protocol errors. Implementation diagnostics must remain
/// in server-side logs and must not be placed in this envelope.
#[derive(Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct ResponseEnvelope {
    pub protocol_version: u16,
    pub request_id: u64,
    pub result: Result<ResponsePayload, IpcError>,
}

impl fmt::Debug for ResponseEnvelope {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Avoid accidentally logging transaction details or bearer tokens when
        // an entire response envelope is included in a diagnostic message.
        let result_summary = if self.result.is_ok() {
            "Ok(<payload redacted>)"
        } else {
            "Err(<protocol error>)"
        };
        formatter
            .debug_struct("ResponseEnvelope")
            .field("protocol_version", &self.protocol_version)
            .field("request_id", &self.request_id)
            .field("result", &result_summary)
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use super::{RequestEnvelope, ResponseEnvelope};
    use crate::command::Command;
    use crate::error::{ErrorCode, IpcError};
    use crate::response::ResponsePayload;
    use crate::version::IPC_PROTOCOL_VERSION;

    #[test]
    fn absent_session_token_is_omitted_from_json() {
        let request = RequestEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: 7,
            session_token: None,
            command: Command::Login {
                email: "person@example.com".to_owned(),
                password: "not-a-real-password".to_owned(),
            },
        };

        let encoded = serde_json::to_string(&request).expect("serialize request");
        assert!(!encoded.contains("session_token"));

        let decoded: RequestEnvelope = serde_json::from_str(&encoded).expect("deserialize request");
        assert_eq!(decoded, request);
    }

    #[test]
    fn request_debug_redacts_bearer_tokens_and_passwords() {
        let request = RequestEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: 8,
            session_token: Some("secret-session-token".to_owned()),
            command: Command::Login {
                email: "person@example.com".to_owned(),
                password: "secret-password".to_owned(),
            },
        };
        let debug = format!("{request:?}");
        assert!(debug.contains("[REDACTED]"));
        assert!(!debug.contains("secret-session-token"));
        assert!(!debug.contains("secret-password"));
    }

    #[test]
    fn response_envelope_round_trips_success_and_error_shapes() {
        let success = ResponseEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: 9,
            result: Ok(ResponsePayload::Transactions(crate::dto::TransactionPage {
                page: 1,
                page_size: 1,
                has_more: false,
                items: Vec::new(),
            })),
        };
        let encoded = serde_json::to_vec(&success).expect("serialize success");
        let decoded: ResponseEnvelope =
            serde_json::from_slice(&encoded).expect("deserialize success");
        assert_eq!(decoded, success);

        let failure = ResponseEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: 10,
            result: Err(IpcError {
                code: ErrorCode::Busy,
            }),
        };
        let encoded = serde_json::to_vec(&failure).expect("serialize failure");
        let decoded: ResponseEnvelope =
            serde_json::from_slice(&encoded).expect("deserialize failure");
        assert_eq!(decoded, failure);
    }
}
