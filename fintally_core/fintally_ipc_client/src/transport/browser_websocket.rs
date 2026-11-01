//! Browser/WASM transport over a secured WebSocket gateway.
//!
//! WebSocket message boundaries replace the native 16-byte stream-frame header;
//! the message body is the same bounded JSON IPC payload. The gateway must enforce
//! payload length, TLS, origin policy, authentication limits, and rate limits.

use crate::error::{ClientError, TransportError};
use crate::transport::PayloadTransport;
use fintally_ipc_protocol::MAX_PAYLOAD_BYTES;
use futures_util::{SinkExt, StreamExt};
use gloo_net::websocket::futures::WebSocket;
use gloo_net::websocket::Message;
use std::future::Future;
use zeroize::Zeroize;

pub struct BrowserWebSocketTransport {
    socket: WebSocket,
}

impl BrowserWebSocketTransport {
    /// Connect to a configured WebSocket gateway (`wss://` in production).
    /// Private endpoints and credentials must not be hard-coded into WASM.
    pub fn connect(url: &str) -> Result<Self, ClientError> {
        let is_secure = has_secure_websocket_authority(url);
        let is_local_debug = cfg!(debug_assertions) && is_loopback_debug_url(url);
        if !(is_secure || is_local_debug) {
            return Err(ClientError::ConnectFailed);
        }
        let socket = WebSocket::open(url).map_err(|_| ClientError::ConnectFailed)?;
        Ok(Self { socket })
    }
}

impl PayloadTransport for BrowserWebSocketTransport {
    fn exchange_payload<'a>(
        &'a mut self,
        payload: &'a [u8],
    ) -> impl Future<Output = Result<Vec<u8>, TransportError>> + 'a {
        async move {
            if payload.len() > MAX_PAYLOAD_BYTES {
                return Err(TransportError::PayloadTooLarge);
            }

            self.socket
                .send(Message::Bytes(payload.to_vec()))
                .await
                .map_err(|_| TransportError::Io)?;

            match self.socket.next().await {
                Some(Ok(Message::Bytes(mut bytes))) => {
                    if bytes.len() > MAX_PAYLOAD_BYTES {
                        bytes.zeroize();
                        return Err(TransportError::PayloadTooLarge);
                    }
                    Ok(bytes)
                }
                // Only binary payload messages are accepted; text messages are
                // rejected so transport framing stays deterministic.
                Some(Ok(Message::Text(mut text))) => {
                    text.zeroize();
                    Err(TransportError::InvalidFrameLength)
                }
                Some(Err(_)) => Err(TransportError::Io),
                None => Err(TransportError::Closed),
            }
        }
    }
}

/// Require a non-empty WSS authority and reject URL user-info in browser config.
fn has_secure_websocket_authority(url: &str) -> bool {
    let Some(rest) = url.strip_prefix("wss://") else {
        return false;
    };
    let authority = rest
        .split(|character| matches!(character, '/' | '?' | '#'))
        .next()
        .unwrap_or_default();
    !authority.is_empty() && !authority.contains('@')
}

/// Development-only plain WebSocket URLs are limited to exact loopback hostnames.
fn is_loopback_debug_url(url: &str) -> bool {
    let Some(rest) = url.strip_prefix("ws://") else {
        return false;
    };
    let authority = rest
        .split(|character| matches!(character, '/' | '?' | '#'))
        .next()
        .unwrap_or_default();
    if authority.is_empty() || authority.contains('@') {
        return false;
    }

    let host = authority
        .rsplit_once(':')
        .map_or(authority, |(host, port)| {
            if port.is_empty() || !port.bytes().all(|byte| byte.is_ascii_digit()) {
                return "";
            }
            host
        });
    matches!(host, "localhost" | "127.0.0.1")
}

#[cfg(test)]
mod tests {
    use super::{has_secure_websocket_authority, is_loopback_debug_url};

    #[test]
    fn websocket_url_validation_rejects_deceptive_loopback_prefixes() {
        assert!(is_loopback_debug_url("ws://localhost:8080/ws/fintally-ipc"));
        assert!(is_loopback_debug_url("ws://127.0.0.1/ws"));
        assert!(!is_loopback_debug_url("ws://localhost.attacker.example/ws"));
        assert!(!is_loopback_debug_url("ws://127.0.0.1.attacker.example/ws"));
        assert!(!is_loopback_debug_url("ws://user@localhost/ws"));
        assert!(!is_loopback_debug_url("wss://localhost/ws"));
    }

    #[test]
    fn secure_websocket_requires_nonempty_authority_without_userinfo() {
        assert!(has_secure_websocket_authority("wss://api.example.test/ws"));
        assert!(!has_secure_websocket_authority("wss://"));
        assert!(!has_secure_websocket_authority(
            "wss://user@api.example.test/ws"
        ));
        assert!(!has_secure_websocket_authority("ws://api.example.test/ws"));
    }
}
