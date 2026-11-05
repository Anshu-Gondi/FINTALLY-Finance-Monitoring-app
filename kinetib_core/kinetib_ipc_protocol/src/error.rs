//! Stable, implementation-independent IPC error types.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ErrorCode {
    UnsupportedProtocolVersion,
    InvalidRequest,
    Unauthenticated,
    Forbidden,
    NotFound,
    Conflict,
    Busy,
    Internal,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct IpcError {
    pub code: ErrorCode,
}

#[cfg(test)]
mod tests {
    use super::{ErrorCode, IpcError};

    #[test]
    fn error_code_uses_stable_snake_case_wire_name() {
        let error = IpcError {
            code: ErrorCode::UnsupportedProtocolVersion,
        };
        let encoded = serde_json::to_string(&error).expect("serialize error");
        assert!(encoded.contains("unsupported_protocol_version"));
        let decoded: IpcError = serde_json::from_str(&encoded).expect("deserialize error");
        assert_eq!(decoded, error);
    }
}
