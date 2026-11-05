//! Stable client errors. Internal OS and framing details are not exposed publicly.

use kinetib_ipc_protocol::ErrorCode;
use thiserror::Error;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum TransportError {
    #[error("IPC stream operation failed")]
    Io,
    #[error("IPC peer closed the connection")]
    Closed,
    #[error("IPC frame header is invalid")]
    InvalidFrameHeader,
    #[error("IPC frame payload exceeds the size limit")]
    PayloadTooLarge,
    #[error("IPC frame length is inconsistent")]
    InvalidFrameLength,
    #[error("IPC frame buffer allocation failed")]
    AllocationFailed,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum ClientError {
    #[error("IPC connection attempt timed out")]
    ConnectTimeout,
    #[error("IPC connection failed")]
    ConnectFailed,
    #[error("IPC request timed out; this client connection is no longer reusable")]
    RequestTimeout,
    #[error("IPC transport failed; this client connection is no longer reusable")]
    Transport,
    #[error("IPC frame is invalid; this client connection is no longer reusable")]
    InvalidFrame,
    #[error("IPC payload could not be encoded")]
    EncodeFailed,
    #[error("IPC payload is larger than the configured protocol limit")]
    PayloadTooLarge,
    #[error("IPC response is malformed; this client connection is no longer reusable")]
    MalformedResponse,
    #[error("IPC server protocol version is incompatible")]
    IncompatibleProtocolVersion,
    #[error("IPC response request ID does not match the request")]
    RequestIdMismatch,
    #[error("IPC server returned an error: {0:?}")]
    Remote(ErrorCode),
    #[error("login is required before this operation")]
    NotAuthenticated,
    #[error("IPC request ID space is exhausted")]
    RequestIdExhausted,
    #[error("client connection is unusable after a previous transport or protocol failure")]
    ConnectionUnusable,
    #[error("request fields failed protocol validation")]
    InvalidRequest,
    #[error("IPC server returned an unexpected response type")]
    UnexpectedResponse,
    #[error("authentication response was missing required fields")]
    InvalidAuthenticationResponse,
    #[error("server authenticated with a different data-residency mode or provider")]
    SessionModeMismatch,
}
