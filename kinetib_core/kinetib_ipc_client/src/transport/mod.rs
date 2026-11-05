//! Platform transport abstraction.
//!
//! Native byte streams use the shared 16-byte FTIP frame header. WebSockets
//! already provide message boundaries and carry the same bounded JSON payload.

#[cfg(target_arch = "wasm32")]
mod browser_websocket;
#[cfg(not(target_arch = "wasm32"))]
mod connection;
pub mod local_socket;

#[cfg(target_arch = "wasm32")]
pub use browser_websocket::BrowserWebSocketTransport;
#[cfg(not(target_arch = "wasm32"))]
pub use connection::FramedConnection;

use crate::error::TransportError;
use std::future::Future;

/// Native transport methods keep static dispatch and a `Send` future.
#[cfg(not(target_arch = "wasm32"))]
pub trait PayloadTransport: Send {
    fn exchange_payload<'a>(
        &'a mut self,
        payload: &'a [u8],
    ) -> impl Future<Output = Result<Vec<u8>, TransportError>> + Send + 'a;
}

/// Browser transports may wrap JavaScript objects that are intentionally `!Send`.
#[cfg(target_arch = "wasm32")]
pub trait PayloadTransport {
    fn exchange_payload<'a>(
        &'a mut self,
        payload: &'a [u8],
    ) -> impl Future<Output = Result<Vec<u8>, TransportError>> + 'a;
}
