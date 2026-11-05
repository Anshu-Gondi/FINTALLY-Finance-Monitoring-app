//! Tests for the 64 KiB frame and bounded JSON payload contract.

use kinetib_ipc_protocol::{
    decode_payload, encode_payload, CodecError, FRAME_HEADER_BYTES, MAX_FRAME_BYTES,
    MAX_PAYLOAD_BYTES,
};

#[test]
fn frame_and_payload_limits_are_consistent() {
    assert_eq!(MAX_FRAME_BYTES, 64 * 1024);
    assert_eq!(FRAME_HEADER_BYTES + MAX_PAYLOAD_BYTES, MAX_FRAME_BYTES);
}

#[test]
fn encoder_never_returns_a_payload_over_the_limit() {
    let payload = serde_json::json!({"status": "ok", "sequence": 42});
    let encoded = encode_payload(&payload).expect("encode small payload");

    assert!(!encoded.is_empty());
    assert!(encoded.len() <= MAX_PAYLOAD_BYTES);
    assert!(encoded.len() + FRAME_HEADER_BYTES <= MAX_FRAME_BYTES);
}

#[test]
fn encoder_rejects_a_serialized_value_over_the_payload_budget() {
    #[derive(serde::Serialize)]
    struct LargePayload<'a> {
        data: &'a str,
    }

    let data = "x".repeat(MAX_PAYLOAD_BYTES + 32);
    assert_eq!(
        encode_payload(&LargePayload { data: &data }),
        Err(CodecError::PayloadTooLarge)
    );
}

#[test]
fn decoder_accepts_valid_json_padded_to_exact_payload_limit() {
    let mut bytes = br#"{"ok":true}"#.to_vec();
    bytes.resize(MAX_PAYLOAD_BYTES, b' ');

    let value: serde_json::Value = decode_payload(&bytes).expect("decode exact-limit JSON");
    assert_eq!(value["ok"], true);
    assert_eq!(bytes.len() + FRAME_HEADER_BYTES, MAX_FRAME_BYTES);
}

#[test]
fn decoder_rejects_payload_one_byte_over_the_limit() {
    let mut bytes = br#"{"ok":true}"#.to_vec();
    bytes.resize(MAX_PAYLOAD_BYTES + 1, b' ');

    assert_eq!(
        decode_payload::<serde_json::Value>(&bytes),
        Err(CodecError::PayloadTooLarge)
    );
}

#[test]
fn empty_or_small_invalid_payloads_are_malformed_not_oversized() {
    assert_eq!(
        decode_payload::<serde_json::Value>(b"not-json"),
        Err(CodecError::MalformedPayload)
    );
}
