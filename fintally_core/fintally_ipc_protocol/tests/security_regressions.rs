//! Regression tests for credential redaction and request-validation boundaries.

use fintally_ipc_protocol::{
    Command, ErrorCode, IpcError, LoginResult, RequestEnvelope, RequestValidationError,
    ResponseEnvelope, ResponsePayload, TransactionPage, DEFAULT_PAGE_SIZE, IPC_PROTOCOL_VERSION,
    MAX_EMAIL_BYTES, MAX_PASSWORD_BYTES, MAX_SESSION_TOKEN_BYTES,
};

fn list_request(token: Option<&str>) -> RequestEnvelope {
    RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 77,
        session_token: token.map(str::to_owned),
        command: Command::ListTransactions {
            page: 1,
            page_size: DEFAULT_PAGE_SIZE,
        },
    }
}

#[test]
fn request_debug_redacts_password_and_session_token() {
    let request = RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 1,
        // Deliberately invalid for a login request: debug should redact every token regardless.
        session_token: Some("secret-bearer-token".to_owned()),
        command: Command::Login {
            email: "person@example.test".to_owned(),
            password: "secret-password".to_owned(),
        },
    };

    let debug = format!("{request:?}");
    assert!(debug.contains("[REDACTED]"));
    assert!(!debug.contains("secret-bearer-token"));
    assert!(!debug.contains("secret-password"));
}

#[test]
fn login_result_debug_redacts_issued_token() {
    let result = LoginResult {
        session_token: "secret-issued-token".to_owned(),
    };
    let debug = format!("{result:?}");
    assert!(debug.contains("[REDACTED]"));
    assert!(!debug.contains("secret-issued-token"));
}

#[test]
fn response_envelope_debug_redacts_response_payload() {
    let response = ResponseEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 2,
        result: Ok(ResponsePayload::Login(LoginResult {
            session_token: "secret-response-token".to_owned(),
        })),
    };
    let debug = format!("{response:?}");
    assert!(debug.contains("payload redacted"));
    assert!(!debug.contains("secret-response-token"));
}

#[test]
fn public_error_schema_contains_only_a_stable_code() {
    let error = IpcError {
        code: ErrorCode::Internal,
    };
    let value = serde_json::to_value(error).expect("serialize protocol error");
    assert_eq!(value, serde_json::json!({"code": "internal"}));
}

#[test]
fn validation_applies_utf8_byte_limits_not_character_counts() {
    let too_long_email = "é".repeat(MAX_EMAIL_BYTES / 2 + 1);
    let request = RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 1,
        session_token: None,
        command: Command::Login {
            email: too_long_email,
            password: "p".to_owned(),
        },
    };
    assert_eq!(
        request.validate(),
        Err(RequestValidationError::InvalidCredentialsPayload)
    );

    let request = RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 2,
        session_token: None,
        command: Command::Login {
            email: "person@example.test".to_owned(),
            password: "p".repeat(MAX_PASSWORD_BYTES + 1),
        },
    };
    assert_eq!(
        request.validate(),
        Err(RequestValidationError::InvalidCredentialsPayload)
    );
}

#[test]
fn rejects_missing_blank_and_oversized_tokens() {
    assert_eq!(
        list_request(None).validate(),
        Err(RequestValidationError::MissingSessionToken)
    );
    assert_eq!(
        list_request(Some("  ")).validate(),
        Err(RequestValidationError::InvalidSessionToken)
    );
    let oversized_token = "t".repeat(MAX_SESSION_TOKEN_BYTES + 1);
    assert_eq!(
        list_request(Some(&oversized_token)).validate(),
        Err(RequestValidationError::InvalidSessionToken)
    );
}

#[test]
fn validation_is_not_authentication() {
    // Structural validation only checks placement and size. The service must
    // still authenticate/authorize this opaque token before accessing data.
    assert_eq!(
        list_request(Some("structurally-valid-but-not-authenticated")).validate(),
        Ok(())
    );
}

#[test]
fn serialized_response_does_not_add_implementation_diagnostics() {
    let response = ResponseEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 3,
        result: Ok(ResponsePayload::Transactions(TransactionPage {
            page: 1,
            page_size: 1,
            has_more: false,
            items: Vec::new(),
        })),
    };
    let wire = serde_json::to_vec(&response).expect("serialize wire response");
    let text = String::from_utf8(wire).expect("JSON is UTF-8");
    assert!(!text.contains("stack trace"));
    assert!(!text.contains("SQLx"));
    assert!(!text.contains("database password"));
}
