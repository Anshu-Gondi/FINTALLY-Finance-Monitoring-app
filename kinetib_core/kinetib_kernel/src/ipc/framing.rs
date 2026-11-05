//! Bounded 16-byte frame transport. Validate length before allocating payload.

use kinetib_ipc_protocol::{
    decode_frame_header, encode_frame_header, FrameHeaderError, MAX_REQUEST_PAYLOAD_BYTES,
};
use std::io;
use thiserror::Error;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

#[derive(Debug, Error)]
pub enum FrameIoError {
    #[error("IPC transport I/O failed")]
    Io(#[from] io::Error),
    #[error("invalid IPC frame header")]
    Header(#[from] FrameHeaderError),
    #[error("IPC frame payload allocation failed")]
    AllocationFailed,
    #[error("IPC payload exceeds the configured size limit")]
    PayloadTooLarge,
}

/// Read one payload. `Ok(None)` means clean EOF before any header byte.
pub async fn read_payload<R>(reader: &mut R) -> Result<Option<Vec<u8>>, FrameIoError>
where
    R: AsyncRead + Unpin,
{
    let mut header = [0_u8; kinetib_ipc_protocol::FRAME_HEADER_BYTES];
    let first_byte_count = reader.read(&mut header[..1]).await?;
    if first_byte_count == 0 {
        return Ok(None);
    }
    reader.read_exact(&mut header[1..]).await?;

    // The length is checked before allocating the frame body.
    let payload_len = decode_frame_header(&header)?;
    if payload_len > MAX_REQUEST_PAYLOAD_BYTES {
        return Err(FrameIoError::PayloadTooLarge);
    }

    let mut payload = Vec::new();
    payload
        .try_reserve_exact(payload_len)
        .map_err(|_| FrameIoError::AllocationFailed)?;
    payload.resize(payload_len, 0);
    reader.read_exact(&mut payload).await?;
    Ok(Some(payload))
}

pub async fn write_payload<W>(writer: &mut W, payload: &[u8]) -> Result<(), FrameIoError>
where
    W: AsyncWrite + Unpin,
{
    let header = encode_frame_header(payload.len())?;
    writer.write_all(&header).await?;
    writer.write_all(payload).await?;
    writer.flush().await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{read_payload, write_payload};
    use tokio::io::duplex;

    #[tokio::test]
    async fn frame_round_trip_over_bounded_duplex_transport() {
        let (mut client, mut server) = duplex(256);
        let payload = br#"{"ok":true}"#.to_vec();
        let writer = tokio::spawn(async move {
            write_payload(&mut client, &payload)
                .await
                .expect("write frame");
        });
        let decoded = read_payload(&mut server)
            .await
            .expect("read frame")
            .expect("frame");
        writer.await.expect("writer task");
        assert_eq!(decoded, br#"{"ok":true}"#);
    }

    #[tokio::test]
    async fn rejects_oversized_declared_length_before_reading_payload() {
        use kinetib_ipc_protocol::{encode_frame_header, FrameHeaderError, MAX_PAYLOAD_BYTES};
        use tokio::io::AsyncWriteExt;

        let (mut client, mut server) = duplex(64);
        let mut header = encode_frame_header(0).expect("zero-length header");
        header[8..12].copy_from_slice(&((MAX_PAYLOAD_BYTES + 1) as u32).to_le_bytes());
        client
            .write_all(&header)
            .await
            .expect("write malicious header");
        drop(client); // No body is sent; rejection must happen from the header alone.

        let error = read_payload(&mut server)
            .await
            .expect_err("reject oversized length");
        assert!(matches!(
            error,
            super::FrameIoError::Header(FrameHeaderError::PayloadTooLarge)
        ));
    }
    #[tokio::test]
    async fn rejects_request_limit_before_allocating_payload() {
        use kinetib_ipc_protocol::{encode_frame_header, MAX_REQUEST_PAYLOAD_BYTES};
        use tokio::io::AsyncWriteExt;

        let (mut client, mut server) = duplex(64);
        let header = encode_frame_header(MAX_REQUEST_PAYLOAD_BYTES + 1)
            .expect("request still fits the transport frame");
        client.write_all(&header).await.expect("write frame header");
        drop(client);

        let error = read_payload(&mut server)
            .await
            .expect_err("reject request before body allocation");
        assert!(matches!(error, super::FrameIoError::PayloadTooLarge));
    }
}
