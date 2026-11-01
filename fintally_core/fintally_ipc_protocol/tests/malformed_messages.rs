//! Negative parser tests for malformed or adversarial JSON input.

use fintally_ipc_protocol::{decode_payload, CodecError, RequestEnvelope};

fn rejects(input: &[u8]) {
    assert!(
        decode_payload::<RequestEnvelope>(input).is_err(),
        "unexpectedly accepted input: {:?}",
        String::from_utf8_lossy(input)
    );
}

#[test]
fn rejects_empty_truncated_and_invalid_json() {
    rejects(b"");
    rejects(b"{");
    rejects(b"{not-json}");
    rejects(br#"{"protocol_version":1,"request_id":1"#);
}

#[test]
fn rejects_wrong_top_level_types() {
    rejects(b"null");
    rejects(b"[]");
    rejects(b"42");
    rejects(br#""a string""#);
}

#[test]
fn rejects_missing_required_fields_and_wrong_field_types() {
    rejects(br#"{"protocol_version":1,"request_id":1}"#);
    rejects(br#"{"protocol_version":"1","request_id":1,"command":{"type":"login","payload":{"email":"a@example.test","password":"p"}}}"#);
    rejects(br#"{"protocol_version":1,"request_id":"one","command":{"type":"login","payload":{"email":"a@example.test","password":"p"}}}"#);
}

#[test]
fn rejects_unknown_envelope_and_command_fields() {
    rejects(br#"{"protocol_version":1,"request_id":1,"surprise":true,"command":{"type":"login","payload":{"email":"a@example.test","password":"p"}}}"#);
    rejects(br#"{"protocol_version":1,"request_id":1,"command":{"type":"login","payload":{"email":"a@example.test","password":"p","admin":true}}}"#);
}

#[test]
fn rejects_unknown_enum_variants_and_invalid_payload_shapes() {
    rejects(br#"{"protocol_version":1,"request_id":1,"command":{"type":"delete_everything","payload":{}}}"#);
    rejects(br#"{"protocol_version":1,"request_id":1,"command":{"type":"login","payload":null}}"#);
    rejects(br#"{"protocol_version":1,"request_id":1,"command":{"type":"list_transactions","payload":{"page":1}}}"#);
}

#[test]
fn rejects_duplicate_fields_and_trailing_garbage() {
    rejects(br#"{"protocol_version":1,"request_id":1,"request_id":2,"command":{"type":"login","payload":{"email":"a@example.test","password":"p"}}}"#);
    rejects(br#"{"protocol_version":1,"request_id":1,"command":{"type":"login","payload":{"email":"a@example.test","password":"p"}}} trailing"#);
}

#[test]
fn oversized_input_fails_with_a_stable_size_error() {
    let bytes = vec![b' '; fintally_ipc_protocol::MAX_PAYLOAD_BYTES + 1];
    assert_eq!(
        decode_payload::<RequestEnvelope>(&bytes),
        Err(CodecError::PayloadTooLarge)
    );
}

#[test]
fn deterministic_arbitrary_bytes_do_not_panic_the_decoder() {
    // A small deterministic smoke test. This is not a replacement for cargo-fuzz,
    // but it checks decoder robustness without another test dependency.
    let mut state: u64 = 0x9E37_79B9_7F4A_7C15;
    for length in 0..=512 {
        let mut input = vec![0_u8; length];
        for byte in &mut input {
            state = state
                .wrapping_mul(6_364_136_223_846_793_005)
                .wrapping_add(1_442_695_040_888_963_407);
            *byte = (state >> 32) as u8;
        }
        let _ = decode_payload::<RequestEnvelope>(&input);
    }
}
