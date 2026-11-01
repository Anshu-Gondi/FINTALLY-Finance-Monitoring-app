//! One persistent local IPC connection, one bounded frame at a time.

use crate::ipc::dispatch::dispatch;
use crate::ipc::framing::{read_payload, write_payload, FrameIoError};
use crate::observability::metrics::Metrics;
use crate::state::KernelState;
use fintally_ipc_protocol::{
    decode_payload, encode_payload, CodecError, ErrorCode, IpcError, RequestEnvelope,
    ResponseEnvelope, IPC_PROTOCOL_VERSION,
};
use std::sync::Arc;
use std::time::Duration;
use thiserror::Error;
use tokio::net::UnixStream;
use tokio::time::timeout;

const FRAME_READ_TIMEOUT: Duration = Duration::from_secs(30);

#[derive(Debug, Error)]
pub enum ConnectionError {
    #[error("IPC framing failed")]
    Framing(#[from] FrameIoError),
    #[error("IPC payload encoding failed")]
    Encoding(CodecError),
    #[error("IPC connection timed out")]
    Timeout,
}

pub async fn serve_connection(
    mut stream: UnixStream,
    state: Arc<KernelState>,
    metrics: Arc<Metrics>,
) -> Result<(), ConnectionError> {
    loop {
        let payload = timeout(FRAME_READ_TIMEOUT, read_payload(&mut stream))
            .await
            .map_err(|_| ConnectionError::Timeout)??;
        let Some(payload) = payload else {
            return Ok(());
        };

        let request: RequestEnvelope = match decode_payload(&payload) {
            Ok(request) => request,
            Err(error) => {
                metrics.frame_rejected();
                tracing::warn!(error = ?error, "closing connection after malformed IPC payload");
                return Ok(()); // No trustworthy request_id is available to correlate an error.
            }
        };
        // The request is fully owned after deserialization; release the raw
        // frame buffer before building a potentially large response.
        drop(payload);

        metrics.request_received();
        let response = dispatch(&state, request).await;
        let encoded = match encode_payload(&response) {
            Ok(encoded) => encoded,
            Err(CodecError::PayloadTooLarge) => {
                // Never attempt to send an oversized response. Fall back to a
                // minimal stable error envelope; this should be rare because
                // the application service already applies a conservative cap.
                let fallback = ResponseEnvelope {
                    protocol_version: IPC_PROTOCOL_VERSION,
                    request_id: response.request_id,
                    result: Err(IpcError {
                        code: ErrorCode::Internal,
                    }),
                };
                encode_payload(&fallback).map_err(ConnectionError::Encoding)?
            }
            Err(error) => return Err(ConnectionError::Encoding(error)),
        };
        write_payload(&mut stream, &encoded).await?;
        metrics.response_sent();
    }
}
