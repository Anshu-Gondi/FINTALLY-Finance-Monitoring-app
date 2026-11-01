//! Stable 16-byte transport frame header shared by the IPC client and kernel.
//!
//! Layout (all integers little-endian):
//! - bytes 0..4: ASCII magic `FTIP`
//! - bytes 4..6: framing version (`1`)
//! - bytes 6..8: flags (must be zero in v1)
//! - bytes 8..12: payload length in bytes
//! - bytes 12..16: reserved (must be zero)
//!
//! The payload is the versioned JSON envelope encoded by `codec.rs`. This
//! framing version is independent of `IPC_PROTOCOL_VERSION`.

use crate::limits::{FRAME_HEADER_BYTES, MAX_PAYLOAD_BYTES};
use std::fmt;

pub const FRAME_MAGIC: [u8; 4] = *b"FTIP";
pub const FRAME_FORMAT_VERSION: u16 = 1;

const _: () = assert!(FRAME_HEADER_BYTES == 16);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FrameHeaderError {
    InvalidHeaderLength,
    InvalidMagic,
    UnsupportedVersion,
    UnsupportedFlags,
    NonZeroReservedBytes,
    PayloadTooLarge,
}

impl fmt::Display for FrameHeaderError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let message = match self {
            Self::InvalidHeaderLength => "invalid IPC frame header length",
            Self::InvalidMagic => "invalid IPC frame magic",
            Self::UnsupportedVersion => "unsupported IPC framing version",
            Self::UnsupportedFlags => "unsupported IPC frame flags",
            Self::NonZeroReservedBytes => "IPC frame reserved bytes must be zero",
            Self::PayloadTooLarge => "IPC frame payload exceeds the configured limit",
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for FrameHeaderError {}

/// Encode the 16-byte transport header for a bounded JSON payload.
pub fn encode_frame_header(
    payload_len: usize,
) -> Result<[u8; FRAME_HEADER_BYTES], FrameHeaderError> {
    if payload_len > MAX_PAYLOAD_BYTES {
        return Err(FrameHeaderError::PayloadTooLarge);
    }

    let mut header = [0_u8; FRAME_HEADER_BYTES];
    header[0..4].copy_from_slice(&FRAME_MAGIC);
    header[4..6].copy_from_slice(&FRAME_FORMAT_VERSION.to_le_bytes());
    header[6..8].copy_from_slice(&0_u16.to_le_bytes()); // flags, v1 requires zero
    header[8..12].copy_from_slice(&(payload_len as u32).to_le_bytes());
    // bytes 12..16 are reserved and remain zero
    Ok(header)
}

/// Decode and validate a complete 16-byte header before allocating its payload.
pub fn decode_frame_header(header: &[u8]) -> Result<usize, FrameHeaderError> {
    if header.len() != FRAME_HEADER_BYTES {
        return Err(FrameHeaderError::InvalidHeaderLength);
    }
    if header[0..4] != FRAME_MAGIC[..] {
        return Err(FrameHeaderError::InvalidMagic);
    }

    let version = u16::from_le_bytes([header[4], header[5]]);
    if version != FRAME_FORMAT_VERSION {
        return Err(FrameHeaderError::UnsupportedVersion);
    }

    let flags = u16::from_le_bytes([header[6], header[7]]);
    if flags != 0 {
        return Err(FrameHeaderError::UnsupportedFlags);
    }
    if header[12..16].iter().any(|byte| *byte != 0) {
        return Err(FrameHeaderError::NonZeroReservedBytes);
    }

    let payload_len = u32::from_le_bytes([header[8], header[9], header[10], header[11]]) as usize;
    if payload_len > MAX_PAYLOAD_BYTES {
        return Err(FrameHeaderError::PayloadTooLarge);
    }
    Ok(payload_len)
}

#[cfg(test)]
mod tests {
    use super::{decode_frame_header, encode_frame_header, FrameHeaderError};
    use crate::limits::{FRAME_HEADER_BYTES, MAX_PAYLOAD_BYTES};

    #[test]
    fn header_round_trip_respects_exact_frame_budget() {
        let header = encode_frame_header(MAX_PAYLOAD_BYTES).expect("maximum payload header");
        assert_eq!(header.len(), FRAME_HEADER_BYTES);
        assert_eq!(decode_frame_header(&header), Ok(MAX_PAYLOAD_BYTES));
    }

    #[test]
    fn header_rejects_payload_one_byte_over_limit() {
        assert_eq!(
            encode_frame_header(MAX_PAYLOAD_BYTES + 1),
            Err(FrameHeaderError::PayloadTooLarge)
        );
    }

    #[test]
    fn header_rejects_bad_magic_version_flags_and_reserved_bytes() {
        let valid = encode_frame_header(3).expect("small header");

        let mut invalid = valid;
        invalid[0] = b'X';
        assert_eq!(
            decode_frame_header(&invalid),
            Err(FrameHeaderError::InvalidMagic)
        );

        let mut invalid = valid;
        invalid[4] = 2;
        assert_eq!(
            decode_frame_header(&invalid),
            Err(FrameHeaderError::UnsupportedVersion)
        );

        let mut invalid = valid;
        invalid[6] = 1;
        assert_eq!(
            decode_frame_header(&invalid),
            Err(FrameHeaderError::UnsupportedFlags)
        );

        let mut invalid = valid;
        invalid[15] = 1;
        assert_eq!(
            decode_frame_header(&invalid),
            Err(FrameHeaderError::NonZeroReservedBytes)
        );
    }
}
