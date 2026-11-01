//! Bounded JSON payload encoding and decoding.
//!
//! The codec operates on payload bytes, not the transport frame header.
//! `MAX_PAYLOAD_BYTES` must already reserve room for `FRAME_HEADER_BYTES` so
//! `FRAME_HEADER_BYTES + payload.len()` never exceeds `MAX_FRAME_BYTES`.
//!
//! JSON is used deliberately because this protocol uses Serde's adjacently
//! tagged enum representation (`type` and `payload`). The tests pin
//! that representation with round-trip checks. Transport code must reject an
//! oversized declared frame length before allocating or reading its payload.

use serde::de::DeserializeOwned;
use serde::Serialize;
use std::io::{self, Write};

use crate::limits::{FRAME_HEADER_BYTES, MAX_FRAME_BYTES, MAX_PAYLOAD_BYTES};

/// Safe, stable codec failure categories. Do not expose parser details or
/// serialization diagnostics directly to remote clients.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CodecError {
    PayloadTooLarge,
    MalformedPayload,
    SerializationFailed,
}

/// Encode one protocol value as JSON while bounding the output buffer.
///
/// This serializes a payload only; the transport must prepend its frame header
/// separately. The writer never appends bytes beyond `MAX_PAYLOAD_BYTES`.
pub fn encode_payload<T: Serialize>(value: &T) -> Result<Vec<u8>, CodecError> {
    let mut writer = BoundedWriter::new(payload_limit());
    let result = serde_json::to_writer(&mut writer, value);

    if writer.exceeded_limit() {
        return Err(CodecError::PayloadTooLarge);
    }

    if result.is_err() {
        return Err(CodecError::SerializationFailed);
    }

    Ok(writer.into_inner())
}

/// Decode a JSON payload after checking its byte length.
///
/// The caller must apply frame-header and transport-level checks before this
/// function. The input slice is already allocated by the caller, so a real
/// transport should enforce the limit before buffering untrusted frame bytes.
pub fn decode_payload<T: DeserializeOwned>(bytes: &[u8]) -> Result<T, CodecError> {
    if bytes.len() > payload_limit()
        || bytes.len().saturating_add(FRAME_HEADER_BYTES) > MAX_FRAME_BYTES
    {
        return Err(CodecError::PayloadTooLarge);
    }

    serde_json::from_slice(bytes).map_err(|_| CodecError::MalformedPayload)
}

#[inline]
const fn payload_limit() -> usize {
    // Honor both limits even if a future edit accidentally makes them
    // inconsistent. `limits.rs` should still assert the intended equality.
    let frame_payload_limit = MAX_FRAME_BYTES - FRAME_HEADER_BYTES;
    if MAX_PAYLOAD_BYTES < frame_payload_limit {
        MAX_PAYLOAD_BYTES
    } else {
        frame_payload_limit
    }
}

struct BoundedWriter {
    bytes: Vec<u8>,
    limit: usize,
    exceeded: bool,
}

impl BoundedWriter {
    fn new(limit: usize) -> Self {
        Self {
            bytes: Vec::new(),
            limit,
            exceeded: false,
        }
    }

    fn exceeded_limit(&self) -> bool {
        self.exceeded
    }

    fn into_inner(self) -> Vec<u8> {
        self.bytes
    }
}

impl Write for BoundedWriter {
    fn write(&mut self, input: &[u8]) -> io::Result<usize> {
        let remaining = self.limit.saturating_sub(self.bytes.len());
        if input.len() > remaining {
            self.exceeded = true;
            return Err(io::Error::new(
                io::ErrorKind::WriteZero,
                "IPC payload exceeds configured size limit",
            ));
        }

        self.bytes
            .try_reserve(input.len())
            .map_err(|_| io::Error::other("IPC output allocation failed"))?;
        self.bytes.extend_from_slice(input);
        Ok(input.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::{decode_payload, encode_payload, CodecError};
    use crate::command::Command;
    use crate::limits::{MAX_PAGE_SIZE, MAX_PAYLOAD_BYTES};
    use crate::version::IPC_PROTOCOL_VERSION;
    use crate::RequestEnvelope;

    #[test]
    fn tagged_enum_wire_shape_is_supported() {
        let request = RequestEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: 17,
            session_token: None,
            command: Command::ListTransactions {
                page: 1,
                page_size: MAX_PAGE_SIZE,
            },
        };

        let bytes = encode_payload(&request).expect("encode request");
        let value: serde_json::Value =
            serde_json::from_slice(&bytes).expect("inspect JSON wire shape");
        assert_eq!(value["command"]["type"], "list_transactions");
        assert_eq!(value["command"]["payload"]["page"], 1);

        let decoded: RequestEnvelope = decode_payload(&bytes).expect("decode request");
        assert_eq!(decoded, request);
    }

    #[test]
    fn refuses_payloads_larger_than_the_configured_budget() {
        #[derive(serde::Serialize)]
        struct LargePayload<'a> {
            data: &'a str,
        }

        let data = "x".repeat(MAX_PAYLOAD_BYTES + 1);
        let error = encode_payload(&LargePayload { data: &data }).unwrap_err();
        assert_eq!(error, CodecError::PayloadTooLarge);
    }

    #[test]
    fn rejects_oversized_input_before_deserialization() {
        let bytes = vec![b' '; MAX_PAYLOAD_BYTES + 1];
        let error = decode_payload::<serde_json::Value>(&bytes).unwrap_err();
        assert_eq!(error, CodecError::PayloadTooLarge);
    }

    #[test]
    fn rejects_malformed_json() {
        let error = decode_payload::<RequestEnvelope>(b"{not-json").unwrap_err();
        assert_eq!(error, CodecError::MalformedPayload);
    }

    #[test]
    fn rejects_unknown_request_fields() {
        let payload = br#"{"protocol_version":3,"request_id":1,"session_token":null,"command":{"type":"list_transactions","payload":{"page":1,"page_size":10}},"unexpected":true}"#;
        let error = decode_payload::<RequestEnvelope>(payload).unwrap_err();
        assert_eq!(error, CodecError::MalformedPayload);
    }
}
