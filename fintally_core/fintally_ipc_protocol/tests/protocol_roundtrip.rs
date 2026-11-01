//! End-to-end serialization round trips for the public IPC wire types.

use fintally_ipc_protocol::{
    decode_payload, encode_payload, BudgetPage, BudgetRecord, Command, CreateBudgetRequest,
    ErrorCode, IpcError, LoginResult, RecurringFrequency, RequestEnvelope, ResponseEnvelope,
    ResponsePayload, TransactionPage, TransactionRecord, IPC_PROTOCOL_VERSION, MAX_PAYLOAD_BYTES,
};

fn sample_record() -> TransactionRecord {
    TransactionRecord {
        id: 41,
        name: "Groceries".to_owned(),
        price: 42.75,
        description: "Weekly food shopping".to_owned(),
        datetime_unix_seconds: 1_800_000_000,
        category: "food".to_owned(),
        is_recurring: true,
        recurring_frequency: Some(RecurringFrequency::Weekly),
        receipt_url: Some("https://receipts.example.test/41".to_owned()),
    }
}

#[test]
fn login_request_round_trips_and_validates() {
    let request = RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 101,
        session_token: None,
        command: Command::Login {
            email: "person@example.test".to_owned(),
            password: "not-a-real-test-password".to_owned(),
        },
    };

    assert_eq!(request.validate(), Ok(()));
    let encoded = encode_payload(&request).expect("encode login request");
    assert!(encoded.len() <= MAX_PAYLOAD_BYTES);

    let decoded: RequestEnvelope = decode_payload(&encoded).expect("decode login request");
    assert_eq!(decoded, request);
    assert_eq!(decoded.validate(), Ok(()));
}

#[test]
fn list_transactions_request_round_trips_with_session_token() {
    let request = RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 102,
        session_token: Some("opaque-test-token".to_owned()),
        command: Command::ListTransactions {
            page: 2,
            page_size: 25,
        },
    };

    assert_eq!(request.validate(), Ok(()));
    let encoded = encode_payload(&request).expect("encode transaction-list request");
    let decoded: RequestEnvelope =
        decode_payload(&encoded).expect("decode transaction-list request");
    assert_eq!(decoded, request);
}

#[test]
fn successful_transaction_response_round_trips() {
    let response = ResponseEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 201,
        result: Ok(ResponsePayload::Transactions(TransactionPage {
            page: 1,
            page_size: 1,
            has_more: false,
            items: vec![sample_record()],
        })),
    };

    let encoded = encode_payload(&response).expect("encode transaction response");
    let decoded: ResponseEnvelope = decode_payload(&encoded).expect("decode transaction response");
    assert_eq!(decoded, response);
}

#[test]
fn login_response_round_trips() {
    let response = ResponseEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 202,
        result: Ok(ResponsePayload::Login(LoginResult {
            session_token: "opaque-issued-test-token".to_owned(),
        })),
    };

    let encoded = encode_payload(&response).expect("encode login response");
    let decoded: ResponseEnvelope = decode_payload(&encoded).expect("decode login response");
    assert_eq!(decoded, response);
}

#[test]
fn protocol_error_response_round_trips_without_diagnostic_text() {
    let response = ResponseEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 203,
        result: Err(IpcError {
            code: ErrorCode::Unauthenticated,
        }),
    };

    let encoded = encode_payload(&response).expect("encode protocol error");
    let decoded: ResponseEnvelope = decode_payload(&encoded).expect("decode protocol error");
    assert_eq!(decoded, response);

    let value: serde_json::Value = serde_json::from_slice(&encoded).expect("inspect error JSON");
    assert!(value.get("database_error").is_none());
    assert!(value.get("stack_trace").is_none());
}

#[test]
fn create_budget_request_round_trips_and_validates_minor_units() {
    let request = RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 301,
        session_token: Some("opaque-budget-token".to_owned()),
        command: Command::CreateBudget {
            budget: CreateBudgetRequest {
                category: "Food".to_owned(),
                amount_minor_units: 800_000,
                period_start_unix: 1_800_000_000,
                period_end_unix: Some(1_802_592_000),
            },
        },
    };

    assert_eq!(request.validate(), Ok(()));
    let encoded = encode_payload(&request).expect("encode budget request");
    let decoded: RequestEnvelope = decode_payload(&encoded).expect("decode budget request");
    assert_eq!(decoded, request);
}

#[test]
fn budget_response_round_trips_with_integer_minor_units() {
    let response = ResponseEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 302,
        result: Ok(ResponsePayload::Budgets(BudgetPage {
            items: vec![BudgetRecord {
                id: 9,
                category: "Food".into(),
                amount_minor_units: 800_000,
                spent_minor_units: 642_500,
                remaining_minor_units: 157_500,
                utilization_percent_x100: 8_031,
                period_start_unix: 1_800_000_000,
                period_end_unix: None,
                is_active: true,
                revision: 2,
            }],
        })),
    };

    let encoded = encode_payload(&response).expect("encode budget response");
    assert!(encoded.len() < MAX_PAYLOAD_BYTES);
    let decoded: ResponseEnvelope = decode_payload(&encoded).expect("decode budget response");
    assert_eq!(decoded, response);
}

#[test]
fn invalid_budget_minor_units_are_rejected_before_dispatch() {
    let request = RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 303,
        session_token: Some("opaque-budget-token".into()),
        command: Command::CreateBudget {
            budget: CreateBudgetRequest {
                category: "Food".into(),
                amount_minor_units: 0,
                period_start_unix: 1_800_000_000,
                period_end_unix: None,
            },
        },
    };
    assert!(request.validate().is_err());
}
