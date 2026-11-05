use kinetib_ipc_client::{ClientConfig, ClientError, IpcClient, PayloadTransport, TransportError};
use kinetib_ipc_protocol::{
    decode_payload, encode_payload, AuthSession, Command, IdentityProvider, IpcError, LogoutResult,
    RequestEnvelope, ResponseEnvelope, ResponsePayload, SessionStatus, StorageMode,
    IPC_PROTOCOL_VERSION,
};
use std::collections::VecDeque;
use std::sync::{Arc, Mutex};

#[derive(Clone)]
struct MockTransport {
    replies: VecDeque<Vec<u8>>,
    sent: Arc<Mutex<Vec<Vec<u8>>>>,
}

impl MockTransport {
    fn new(replies: Vec<Vec<u8>>) -> (Self, Arc<Mutex<Vec<Vec<u8>>>>) {
        let sent = Arc::new(Mutex::new(Vec::new()));
        (
            Self {
                replies: replies.into(),
                sent: Arc::clone(&sent),
            },
            sent,
        )
    }
}

impl PayloadTransport for MockTransport {
    async fn exchange_payload<'a>(
        &'a mut self,
        payload: &'a [u8],
    ) -> Result<Vec<u8>, TransportError> {
        self.sent
            .lock()
            .expect("request capture")
            .push(payload.to_vec());
        self.replies.pop_front().ok_or(TransportError::Closed)
    }
}

fn response(id: u64, result: Result<ResponsePayload, IpcError>) -> Vec<u8> {
    encode_payload(&ResponseEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: id,
        result,
    })
    .expect("encode mock response")
}

fn auth_response(id: u64, mode: StorageMode, provider: IdentityProvider) -> Vec<u8> {
    response(
        id,
        Ok(ResponsePayload::Authenticated(AuthSession {
            session_token: "opaque-session-token".into(),
            storage_mode: mode,
            provider,
            expires_at_unix_seconds: Some(2_000_000_000),
        })),
    )
}

#[tokio::test]
async fn local_signup_sends_explicit_local_only_scope() {
    let (transport, sent) = MockTransport::new(vec![auth_response(
        1,
        StorageMode::LocalOnly,
        IdentityProvider::EmailPassword,
    )]);
    let mut client = IpcClient::new(transport, ClientConfig::default());

    client
        .sign_up(
            "private@example.test",
            "correct-horse-battery",
            StorageMode::LocalOnly,
        )
        .await
        .expect("local signup response");
    assert!(client.has_session());
    assert_eq!(
        client
            .session_metadata()
            .expect("session metadata")
            .storage_mode,
        StorageMode::LocalOnly
    );

    let captured = sent.lock().expect("requests");
    let request: RequestEnvelope = decode_payload(&captured[0]).expect("decode request");
    assert!(matches!(
        request.command,
        Command::SignUp {
            storage_mode: StorageMode::LocalOnly,
            ..
        }
    ));
}

#[tokio::test]
async fn google_signin_keeps_explicit_cloud_sync_choice() {
    let (transport, sent) = MockTransport::new(vec![auth_response(
        1,
        StorageMode::CloudSync,
        IdentityProvider::Google,
    )]);
    let mut client = IpcClient::new(transport, ClientConfig::default());

    client
        .sign_in_with_google("header.payload.signature", StorageMode::CloudSync)
        .await
        .expect("Google sign-in response");
    assert_eq!(
        client.session_metadata().expect("metadata").provider,
        IdentityProvider::Google
    );

    let captured = sent.lock().expect("requests");
    let request: RequestEnvelope = decode_payload(&captured[0]).expect("decode request");
    assert!(matches!(
        request.command,
        Command::GoogleSignIn {
            storage_mode: StorageMode::CloudSync,
            ..
        }
    ));
}

#[tokio::test]
async fn google_signup_sends_explicit_local_only_scope() {
    let (transport, sent) = MockTransport::new(vec![auth_response(
        1,
        StorageMode::LocalOnly,
        IdentityProvider::Google,
    )]);
    let mut client = IpcClient::new(transport, ClientConfig::default());

    client
        .sign_up_with_google("header.payload.signature", StorageMode::LocalOnly)
        .await
        .expect("Google signup response");
    assert_eq!(
        client.session_metadata().expect("metadata").storage_mode,
        StorageMode::LocalOnly
    );

    let captured = sent.lock().expect("requests");
    let request: RequestEnvelope = decode_payload(&captured[0]).expect("decode request");
    assert!(matches!(
        request.command,
        Command::GoogleSignUp {
            storage_mode: StorageMode::LocalOnly,
            ..
        }
    ));
}

#[tokio::test]
async fn mismatched_server_storage_mode_fails_closed() {
    let (transport, _) = MockTransport::new(vec![auth_response(
        1,
        StorageMode::CloudSync,
        IdentityProvider::EmailPassword,
    )]);
    let mut client = IpcClient::new(transport, ClientConfig::default());

    assert!(matches!(
        client
            .sign_in(
                "private@example.test",
                "correct-horse-battery",
                StorageMode::LocalOnly
            )
            .await,
        Err(ClientError::SessionModeMismatch)
    ));
    assert!(!client.has_session());
}

#[tokio::test]
async fn logout_erases_local_session_after_server_acknowledgement() {
    let replies = vec![
        auth_response(1, StorageMode::LocalOnly, IdentityProvider::EmailPassword),
        response(
            2,
            Ok(ResponsePayload::LoggedOut(LogoutResult { revoked: true })),
        ),
    ];
    let (transport, _) = MockTransport::new(replies);
    let mut client = IpcClient::new(transport, ClientConfig::default());
    client
        .sign_in(
            "private@example.test",
            "correct-horse-battery",
            StorageMode::LocalOnly,
        )
        .await
        .expect("sign-in");

    assert!(client.logout().await.expect("logout acknowledgement"));
    assert!(!client.has_session());
    assert!(client.session_metadata().is_none());
}

#[tokio::test]
async fn check_session_updates_metadata_from_kernel_response() {
    let replies = vec![
        auth_response(1, StorageMode::LocalOnly, IdentityProvider::EmailPassword),
        response(
            2,
            Ok(ResponsePayload::SessionStatus(SessionStatus {
                authenticated: true,
                storage_mode: Some(StorageMode::LocalOnly),
                provider: Some(IdentityProvider::EmailPassword),
                expires_at_unix_seconds: Some(2_000_000_000),
            })),
        ),
    ];
    let (transport, _) = MockTransport::new(replies);
    let mut client = IpcClient::new(transport, ClientConfig::default());
    client
        .sign_in(
            "private@example.test",
            "correct-horse-battery",
            StorageMode::LocalOnly,
        )
        .await
        .expect("sign-in");

    assert!(
        client
            .check_session()
            .await
            .expect("session check")
            .authenticated
    );
}
