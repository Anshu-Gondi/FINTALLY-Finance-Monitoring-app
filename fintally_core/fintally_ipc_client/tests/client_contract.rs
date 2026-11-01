use fintally_ipc_client::{ClientConfig, ClientError, IpcClient, PayloadTransport, TransportError};
use fintally_ipc_protocol::{
    decode_payload, encode_payload, Command, ErrorCode, IpcError, LoginResult, RequestEnvelope,
    ResponseEnvelope, ResponsePayload, TransactionPage, IPC_PROTOCOL_VERSION,
};
use std::collections::VecDeque;
use std::future::Future;
use std::sync::{Arc, Mutex};
use std::time::Duration;

#[derive(Clone)]
struct MockTransport {
    replies: VecDeque<Vec<u8>>,
    sent: Arc<Mutex<Vec<Vec<u8>>>>,
    delay: Duration,
}

impl MockTransport {
    fn new(replies: Vec<Vec<u8>>) -> (Self, Arc<Mutex<Vec<Vec<u8>>>>) {
        let sent = Arc::new(Mutex::new(Vec::new()));
        (
            Self {
                replies: replies.into(),
                sent: Arc::clone(&sent),
                delay: Duration::ZERO,
            },
            sent,
        )
    }

    fn delayed(mut self, delay: Duration) -> Self {
        self.delay = delay;
        self
    }
}

impl PayloadTransport for MockTransport {
    // Keep the returned future's `Send` bound explicit for native clients.
    #[allow(clippy::manual_async_fn)]
    fn exchange_payload<'a>(
        &'a mut self,
        payload: &'a [u8],
    ) -> impl Future<Output = Result<Vec<u8>, TransportError>> + Send + 'a {
        async move {
            {
                self.sent
                    .lock()
                    .expect("request recorder mutex")
                    .push(payload.to_vec());
            }
            if !self.delay.is_zero() {
                tokio::time::sleep(self.delay).await;
            }
            self.replies.pop_front().ok_or(TransportError::Closed)
        }
    }
}

fn response_bytes(
    protocol_version: u16,
    request_id: u64,
    result: Result<ResponsePayload, IpcError>,
) -> Vec<u8> {
    encode_payload(&ResponseEnvelope {
        protocol_version,
        request_id,
        result,
    })
    .expect("encode test response")
}

fn login_response(request_id: u64, token: &str) -> Vec<u8> {
    response_bytes(
        IPC_PROTOCOL_VERSION,
        request_id,
        Ok(ResponsePayload::Login(LoginResult {
            session_token: token.to_owned(),
        })),
    )
}

#[tokio::test]
async fn login_and_transactions_preserve_request_correlation_and_session() {
    let replies = vec![
        login_response(1, "opaque-test-token"),
        response_bytes(
            IPC_PROTOCOL_VERSION,
            2,
            Ok(ResponsePayload::Transactions(TransactionPage {
                page: 1,
                page_size: 10,
                has_more: false,
                items: Vec::new(),
            })),
        ),
    ];
    let (transport, sent) = MockTransport::new(replies);
    let mut client = IpcClient::new(transport, ClientConfig::default());

    client
        .login("user@example.test", "correct-test-password")
        .await
        .expect("login response");
    assert!(client.has_session());

    let page = client
        .list_transactions(1, 10)
        .await
        .expect("transaction page");
    assert_eq!(page.page, 1);
    assert!(page.items.is_empty());

    let captured = sent.lock().expect("captured requests");
    assert_eq!(captured.len(), 2);
    let login: RequestEnvelope = decode_payload(&captured[0]).expect("decode login request");
    let list: RequestEnvelope = decode_payload(&captured[1]).expect("decode listing request");
    assert_eq!(login.request_id, 1);
    assert_eq!(list.request_id, 2);
    assert!(matches!(login.command, Command::Login { .. }));
    assert!(matches!(
        list.command,
        Command::ListTransactions {
            page: 1,
            page_size: 10
        }
    ));
    assert_eq!(list.session_token.as_deref(), Some("opaque-test-token"));
}

#[tokio::test]
async fn transaction_request_without_login_never_reaches_transport() {
    let (transport, sent) = MockTransport::new(Vec::new());
    let mut client = IpcClient::new(transport, ClientConfig::default());
    assert!(matches!(
        client.list_transactions(1, 10).await,
        Err(ClientError::NotAuthenticated)
    ));
    assert!(sent.lock().expect("captured requests").is_empty());
}

#[tokio::test]
async fn rejects_a_response_with_the_wrong_request_id_and_poisons_connection() {
    let (transport, _) = MockTransport::new(vec![login_response(99, "opaque-token")]);
    let mut client = IpcClient::new(transport, ClientConfig::default());
    assert!(matches!(
        client
            .login("user@example.test", "correct-test-password")
            .await,
        Err(ClientError::RequestIdMismatch)
    ));
    assert!(matches!(
        client
            .login("user@example.test", "correct-test-password")
            .await,
        Err(ClientError::ConnectionUnusable)
    ));
}

#[tokio::test]
async fn rejects_an_incompatible_protocol_version() {
    let (transport, _) = MockTransport::new(vec![response_bytes(
        IPC_PROTOCOL_VERSION.saturating_add(1),
        1,
        Ok(ResponsePayload::Login(LoginResult {
            session_token: "opaque-token".to_owned(),
        })),
    )]);
    let mut client = IpcClient::new(transport, ClientConfig::default());
    assert!(matches!(
        client
            .login("user@example.test", "correct-test-password")
            .await,
        Err(ClientError::IncompatibleProtocolVersion)
    ));
}

#[tokio::test]
async fn malformed_response_fails_closed() {
    let (transport, _) = MockTransport::new(vec![b"not-json".to_vec()]);
    let mut client = IpcClient::new(transport, ClientConfig::default());
    assert!(matches!(
        client
            .login("user@example.test", "correct-test-password")
            .await,
        Err(ClientError::MalformedResponse)
    ));
}

#[tokio::test]
async fn request_timeout_makes_stream_unusable() {
    let (transport, _) = MockTransport::new(vec![login_response(1, "opaque-token")]);
    let transport = transport.delayed(Duration::from_millis(50));
    let config = ClientConfig::with_timeouts(Duration::from_millis(20), Duration::from_millis(5))
        .expect("valid test timeouts");
    let mut client = IpcClient::new(transport, config);

    assert!(matches!(
        client
            .login("user@example.test", "correct-test-password")
            .await,
        Err(ClientError::RequestTimeout)
    ));
    assert!(matches!(
        client
            .login("user@example.test", "correct-test-password")
            .await,
        Err(ClientError::ConnectionUnusable)
    ));
}

#[tokio::test]
async fn bcrypt_overlong_password_is_rejected_before_transport() {
    let (transport, sent) = MockTransport::new(Vec::new());
    let mut client = IpcClient::new(transport, ClientConfig::default());
    let password = "p".repeat(73);

    assert!(matches!(
        client.login("user@example.test", &password).await,
        Err(ClientError::InvalidRequest)
    ));
    assert!(sent.lock().expect("captured requests").is_empty());
}

#[tokio::test]
async fn remote_error_is_preserved_as_a_stable_code() {
    let reply = response_bytes(
        IPC_PROTOCOL_VERSION,
        1,
        Err(IpcError {
            code: ErrorCode::Unauthenticated,
        }),
    );
    let (transport, _) = MockTransport::new(vec![reply]);
    let mut client = IpcClient::new(transport, ClientConfig::default());
    assert!(matches!(
        client
            .login("user@example.test", "correct-test-password")
            .await,
        Err(ClientError::Remote(ErrorCode::Unauthenticated))
    ));
}

#[tokio::test]
async fn clearing_local_session_prevents_further_authenticated_requests() {
    let (transport, sent) = MockTransport::new(vec![login_response(1, "opaque-token")]);
    let mut client = IpcClient::new(transport, ClientConfig::default());
    client
        .login("user@example.test", "correct-test-password")
        .await
        .expect("login");
    client.clear_local_session();

    assert!(!client.has_session());
    assert!(matches!(
        client.list_transactions(1, 10).await,
        Err(ClientError::NotAuthenticated)
    ));
    assert_eq!(sent.lock().expect("captured requests").len(), 1);
}
