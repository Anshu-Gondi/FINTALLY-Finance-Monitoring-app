//! Informational in-process client benchmark.
//! This does not measure OS transport latency.

use kinetib_ipc_client::{ClientConfig, IpcClient, PayloadTransport, TransportError};
use kinetib_ipc_protocol::{
    decode_payload, encode_payload, AuthSession, Command, ErrorCode, IdentityProvider, IpcError,
    LoginResult, LogoutResult, RequestEnvelope, ResponseEnvelope, ResponsePayload, SessionStatus,
    StorageMode, TransactionPage, IPC_PROTOCOL_VERSION,
};
use std::time::{Duration, Instant};

struct Loopback;

impl PayloadTransport for Loopback {
    async fn exchange_payload<'a>(
        &'a mut self,
        payload: &'a [u8],
    ) -> Result<Vec<u8>, TransportError> {
        let request: RequestEnvelope =
            decode_payload(payload).map_err(|_| TransportError::InvalidFrameLength)?;

        let result = match request.command {
            Command::Login { .. } => Ok(ResponsePayload::Login(LoginResult {
                session_token: "benchmark-session-token".to_owned(),
            })),

            Command::SignUp { storage_mode, .. }
            | Command::LoginWithStorage { storage_mode, .. } => {
                Ok(ResponsePayload::Authenticated(AuthSession {
                    session_token: "benchmark-session-token".to_owned(),
                    storage_mode,
                    provider: IdentityProvider::EmailPassword,
                    expires_at_unix_seconds: None,
                }))
            }

            Command::GoogleSignIn { storage_mode, .. }
            | Command::GoogleSignUp { storage_mode, .. } => {
                Ok(ResponsePayload::Authenticated(AuthSession {
                    session_token: "benchmark-session-token".to_owned(),
                    storage_mode,
                    provider: IdentityProvider::Google,
                    expires_at_unix_seconds: None,
                }))
            }

            Command::Logout => Ok(ResponsePayload::LoggedOut(LogoutResult { revoked: true })),

            Command::CheckSession => Ok(ResponsePayload::SessionStatus(SessionStatus {
                authenticated: true,
                storage_mode: Some(StorageMode::LocalOnly),
                provider: Some(IdentityProvider::EmailPassword),
                expires_at_unix_seconds: None,
            })),

            Command::ListTransactions { page, page_size } => {
                Ok(ResponsePayload::Transactions(TransactionPage {
                    page,
                    page_size,
                    has_more: false,
                    items: Vec::new(),
                }))
            }

            // This benchmark only implements login and list operations.
            // Reject operations outside this benchmark instead of fabricating results.
            Command::CreateTransaction { .. }
            | Command::UpdateTransaction { .. }
            | Command::DeleteTransaction { .. }
            | Command::GetTransactionReceipt { .. }
            | Command::ListBudgets
            | Command::CreateBudget { .. }
            | Command::UpdateBudget { .. }
            | Command::DeleteBudget { .. }
            | Command::SubmitFeedback { .. } => Err(IpcError {
                code: ErrorCode::InvalidRequest,
            }),
        };

        encode_payload(&ResponseEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: request.request_id,
            result,
        })
        .map_err(|_| TransportError::PayloadTooLarge)
    }
}

#[tokio::test]
#[ignore = "run explicitly in release mode; measures client code with an in-process loopback transport"]
async fn reports_client_request_latency() {
    const SAMPLES: usize = 2_000;

    let mut client = IpcClient::new(Loopback, ClientConfig::default());

    client
        .login("benchmark@example.test", "benchmark-password")
        .await
        .expect("loopback login");

    let mut samples = Vec::with_capacity(SAMPLES);

    for _ in 0..SAMPLES {
        let start = Instant::now();

        client
            .list_transactions(1, 10)
            .await
            .expect("loopback list transactions");

        samples.push(start.elapsed());
    }

    samples.sort_unstable();

    let percentile = |p: usize| samples[(samples.len() - 1) * p / 100];
    let micros = |duration: Duration| duration.as_secs_f64() * 1_000_000.0;

    eprintln!(
        "IPC client loopback (no OS transport): n={SAMPLES}, \
        p50={:.2} us, p95={:.2} us, max={:.2} us",
        micros(percentile(50)),
        micros(percentile(95)),
        micros(*samples.last().expect("samples are nonempty")),
    );
}
