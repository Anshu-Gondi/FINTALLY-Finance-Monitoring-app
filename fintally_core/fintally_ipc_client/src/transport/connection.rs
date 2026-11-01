//! Framed transport over an async byte stream (Unix socket or named pipe).

use crate::error::TransportError;
use crate::transport::PayloadTransport;
use fintally_ipc_protocol::{
    decode_frame_header, encode_frame_header, FRAME_HEADER_BYTES, MAX_PAYLOAD_BYTES,
};
use std::future::Future;
use std::io;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

pub struct FramedConnection<S> {
    stream: S,
}

impl<S> FramedConnection<S> {
    pub fn new(stream: S) -> Self {
        Self { stream }
    }

    pub fn into_inner(self) -> S {
        self.stream
    }
}

fn map_io_error(error: io::Error) -> TransportError {
    if error.kind() == io::ErrorKind::UnexpectedEof {
        TransportError::Closed
    } else {
        TransportError::Io
    }
}

impl<S> PayloadTransport for FramedConnection<S>
where
    S: AsyncRead + AsyncWrite + Unpin + Send,
{
    // Keep the returned future's `Send` bound explicit for native clients.
    #[allow(clippy::manual_async_fn)]
    fn exchange_payload<'a>(
        &'a mut self,
        payload: &'a [u8],
    ) -> impl Future<Output = Result<Vec<u8>, TransportError>> + Send + 'a {
        async move {
            if payload.len() > MAX_PAYLOAD_BYTES {
                return Err(TransportError::PayloadTooLarge);
            }

            let header =
                encode_frame_header(payload.len()).map_err(|_| TransportError::PayloadTooLarge)?;
            self.stream.write_all(&header).await.map_err(map_io_error)?;
            self.stream.write_all(payload).await.map_err(map_io_error)?;
            self.stream.flush().await.map_err(map_io_error)?;

            let mut response_header = [0_u8; FRAME_HEADER_BYTES];
            self.stream
                .read_exact(&mut response_header)
                .await
                .map_err(map_io_error)?;
            let response_len = decode_frame_header(&response_header)
                .map_err(|_| TransportError::InvalidFrameHeader)?;
            if response_len > MAX_PAYLOAD_BYTES {
                return Err(TransportError::PayloadTooLarge);
            }

            // Reserve only after the untrusted length has passed frame-header
            // validation. A failed reserve is returned rather than panicking.
            let mut response = Vec::new();
            response
                .try_reserve_exact(response_len)
                .map_err(|_| TransportError::AllocationFailed)?;
            response.resize(response_len, 0);
            self.stream
                .read_exact(&mut response)
                .await
                .map_err(map_io_error)?;
            Ok(response)
        }
    }
}
