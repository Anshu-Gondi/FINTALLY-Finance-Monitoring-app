use fintally_ipc_client::{ClientConfig, ClientError, FramedConnection, IpcClient};
use fintally_ipc_protocol::{
    decode_frame_header, decode_payload, encode_frame_header, encode_payload, Command, LoginResult,
    RequestEnvelope, ResponseEnvelope, ResponsePayload, FRAME_HEADER_BYTES, IPC_PROTOCOL_VERSION,
    MAX_PAYLOAD_BYTES,
};
use tokio::io::{duplex, AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

async fn read_test_payload<S>(stream: &mut S) -> Vec<u8>
where
    S: AsyncRead + Unpin,
{
    let mut header = [0_u8; FRAME_HEADER_BYTES];
    stream
        .read_exact(&mut header)
        .await
        .expect("read request header");
    let length = decode_frame_header(&header).expect("valid request header");
    let mut payload = vec![0_u8; length];
    stream
        .read_exact(&mut payload)
        .await
        .expect("read request body");
    payload
}

async fn write_test_payload<S>(stream: &mut S, payload: &[u8])
where
    S: AsyncWrite + Unpin,
{
    let header = encode_frame_header(payload.len()).expect("valid response header");
    stream
        .write_all(&header)
        .await
        .expect("write response header");
    stream
        .write_all(payload)
        .await
        .expect("write response body");
}

#[tokio::test]
async fn stream_transport_sends_and_receives_the_shared_frame_format() {
    let (client_stream, mut server_stream) = duplex(1024);
    let server = tokio::spawn(async move {
        let request_payload = read_test_payload(&mut server_stream).await;
        let request: RequestEnvelope = decode_payload(&request_payload).expect("decode request");
        assert!(matches!(request.command, Command::Login { .. }));
        let response = ResponseEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: request.request_id,
            result: Ok(ResponsePayload::Login(LoginResult {
                session_token: "integration-token".to_owned(),
            })),
        };
        let payload = encode_payload(&response).expect("encode response");
        write_test_payload(&mut server_stream, &payload).await;
    });

    let transport = FramedConnection::new(client_stream);
    let mut client = IpcClient::new(transport, ClientConfig::default());
    client
        .login("user@example.test", "correct-test-password")
        .await
        .expect("login round trip");
    assert!(client.has_session());
    server.await.expect("server task");
}

#[tokio::test]
async fn stream_transport_rejects_oversized_response_length_before_body_allocation() {
    let (client_stream, mut server_stream) = duplex(1024);
    let server = tokio::spawn(async move {
        let _request_payload = read_test_payload(&mut server_stream).await;
        let mut header = encode_frame_header(0).expect("valid base header");
        header[8..12].copy_from_slice(&((MAX_PAYLOAD_BYTES as u32) + 1).to_le_bytes());
        server_stream
            .write_all(&header)
            .await
            .expect("write malicious header");
    });

    let transport = FramedConnection::new(client_stream);
    let mut client = IpcClient::new(transport, ClientConfig::default());
    assert!(matches!(
        client
            .login("user@example.test", "correct-test-password")
            .await,
        Err(ClientError::InvalidFrame)
    ));
    server.await.expect("server task");
}
