//! Bounded request/response client for the versioned FinTally IPC protocol.
//!
//! The client owns request IDs, validates response correlation, applies request
//! timeouts, bounds payload sizes, and redacts/zeroizes authentication material.
//! Platform transports live behind `PayloadTransport`; see `PLATFORMS.md` for
//! the current desktop implementations and planned WebAssembly integration.

pub mod client;
pub mod config;
pub mod error;
pub mod timeout;
pub mod transport;

pub use client::{IpcClient, SessionMetadata};
pub use config::{ClientConfig, ConfigError};
pub use error::{ClientError, TransportError};
#[cfg(not(target_arch = "wasm32"))]
pub use transport::FramedConnection;
pub use transport::PayloadTransport;

#[cfg(target_arch = "wasm32")]
pub use transport::BrowserWebSocketTransport;
