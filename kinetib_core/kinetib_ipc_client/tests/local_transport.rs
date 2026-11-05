//! OS-level Unix-domain socket integration test.
//!
//! This runs on Linux and other Unix targets, validating the actual connector
//! rather than only the in-memory duplex stream used by `transport_framing.rs`.

#![cfg(unix)]

use kinetib_ipc_client::transport::local_socket::connect_unix;
use kinetib_ipc_client::ClientConfig;
use kinetib_ipc_protocol::{
    decode_frame_header, decode_payload, encode_frame_header, encode_payload, Command, LoginResult,
    RequestEnvelope, ResponseEnvelope, ResponsePayload, FRAME_HEADER_BYTES, IPC_PROTOCOL_VERSION,
};
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::UnixListener;

struct SocketPath(PathBuf);

impl SocketPath {
    fn unique() -> Self {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system clock is after Unix epoch")
            .as_nanos();
        Self(std::env::temp_dir().join(format!("ftipc-{}-{nonce}.sock", std::process::id())))
    }
}

impl Drop for SocketPath {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.0);
    }
}

async fn read_payload(stream: &mut tokio::net::UnixStream) -> Vec<u8> {
    let mut header = [0_u8; FRAME_HEADER_BYTES];
    stream
        .read_exact(&mut header)
        .await
        .expect("read frame header");
    let length = decode_frame_header(&header).expect("validate frame header");
    let mut payload = vec![0_u8; length];
    stream
        .read_exact(&mut payload)
        .await
        .expect("read bounded payload");
    payload
}

async fn write_payload(stream: &mut tokio::net::UnixStream, payload: &[u8]) {
    let header = encode_frame_header(payload.len()).expect("encode bounded header");
    stream.write_all(&header).await.expect("write frame header");
    stream
        .write_all(payload)
        .await
        .expect("write response payload");
}

#[tokio::test]
async fn unix_socket_connector_performs_correlated_login_round_trip() {
    let socket_path = SocketPath::unique();
    let listener = UnixListener::bind(&socket_path.0).expect("bind temporary Unix socket");

    let server = tokio::spawn(async move {
        let (mut stream, _) = listener.accept().await.expect("accept client connection");
        let bytes = read_payload(&mut stream).await;
        let request: RequestEnvelope = decode_payload(&bytes).expect("decode request envelope");
        assert!(matches!(request.command, Command::Login { .. }));

        let response = ResponseEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: request.request_id,
            result: Ok(ResponsePayload::Login(LoginResult {
                session_token: "unix-socket-test-token".to_owned(),
            })),
        };
        let bytes = encode_payload(&response).expect("encode response envelope");
        write_payload(&mut stream, &bytes).await;
    });

    let mut client = connect_unix(&socket_path.0, ClientConfig::default())
        .await
        .expect("connect using the Unix transport");
    client
        .login("linux-test@example.test", "bounded-test-password")
        .await
        .expect("correlated login response");
    assert!(client.has_session());
    server.await.expect("server task completes");
}
