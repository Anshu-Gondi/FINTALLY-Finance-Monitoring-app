//! Tests that intentionally pin important public wire-format choices.
//!
//! If a test here fails after a proposed protocol change, review the change as
//! a compatibility decision rather than mechanically updating the assertion.

use kinetib_ipc_protocol::{
    is_supported_protocol_version, Command, ErrorCode, LoginResult, RequestEnvelope,
    ResponsePayload, TransactionPage, IPC_PROTOCOL_VERSION,
};

#[test]
fn current_protocol_version_is_explicitly_pinned() {
    // Incrementing this value requires an intentional compatibility review.
    assert_eq!(IPC_PROTOCOL_VERSION, 4);
    assert!(is_supported_protocol_version(IPC_PROTOCOL_VERSION));
    assert!(!is_supported_protocol_version(0));
    assert!(!is_supported_protocol_version(u16::MAX));
}

#[test]
fn login_request_wire_shape_is_stable() {
    let request = RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 7,
        session_token: None,
        command: Command::Login {
            email: "person@example.test".to_owned(),
            password: "test-password".to_owned(),
        },
    };

    let value = serde_json::to_value(&request).expect("serialize request");
    assert_eq!(
        value,
        serde_json::json!({
            "protocol_version": 4,
            "request_id": 7,
            "command": {
                "type": "login",
                "payload": {
                    "email": "person@example.test",
                    "password": "test-password"
                }
            }
        })
    );
    assert!(value.get("session_token").is_none());
}

#[test]
fn command_variant_names_are_stable_snake_case() {
    let login = serde_json::to_value(Command::Login {
        email: "a@example.test".to_owned(),
        password: "p".to_owned(),
    })
    .expect("serialize login command");
    assert_eq!(login["type"], "login");

    let list = serde_json::to_value(Command::ListTransactions {
        page: 3,
        page_size: 10,
    })
    .expect("serialize list command");
    assert_eq!(list["type"], "list_transactions");
    assert_eq!(list["payload"]["page_size"], 10);
}

#[test]
fn response_variant_names_and_dto_fields_are_stable() {
    let response = ResponsePayload::Transactions(TransactionPage {
        page: 2,
        page_size: 1,
        has_more: false,
        items: Vec::new(),
    });
    let value = serde_json::to_value(&response).expect("serialize transaction response payload");
    assert_eq!(value["type"], "transactions");
    assert_eq!(value["payload"]["page"], 2);
    assert_eq!(value["payload"]["page_size"], 1);
    assert_eq!(value["payload"]["items"], serde_json::json!([]));

    let login = ResponsePayload::Login(LoginResult {
        session_token: "test-token".to_owned(),
    });
    let value = serde_json::to_value(&login).expect("serialize login response payload");
    assert_eq!(value["type"], "login");
    assert_eq!(value["payload"]["session_token"], "test-token");
}

#[test]
fn error_code_names_are_stable_snake_case() {
    assert_eq!(
        serde_json::to_string(&ErrorCode::UnsupportedProtocolVersion).unwrap(),
        r#""unsupported_protocol_version""#
    );
    assert_eq!(
        serde_json::to_string(&ErrorCode::Unauthenticated).unwrap(),
        r#""unauthenticated""#
    );
}
