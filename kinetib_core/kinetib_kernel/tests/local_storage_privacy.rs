//! Regression tests for explicit local-only authentication and cloud isolation.

use kinetib_db_local::LocalDbContext;
use kinetib_ipc_protocol::{
    Command, ErrorCode, FeedbackCategory, FeedbackPlatform, FeedbackSubmissionRequest,
    RequestEnvelope, ResponsePayload, StorageMode, IPC_PROTOCOL_VERSION,
};
use kinetib_kernel::config::KernelConfig;
use kinetib_kernel::observability::metrics::Metrics;
use kinetib_kernel::state::KernelState;
use std::sync::Arc;
use std::time::Duration;
use std::time::{SystemTime, UNIX_EPOCH};

fn temp_db_path() -> std::path::PathBuf {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("system clock after epoch")
        .as_nanos();
    std::env::temp_dir().join(format!("kinetib-kernel-local-{nonce}.sqlite3"))
}

fn config(path: std::path::PathBuf) -> KernelConfig {
    KernelConfig {
        database_url: None,
        local_db_path: path,
        socket_path: std::env::temp_dir().join("kinetib-kernel-test.sock"),
        session_ttl: Duration::from_secs(60),
        max_sessions: 4,
        max_connections: 2,
        db_max_connections: 1,
        login_attempts_per_window: 5,
        login_window: Duration::from_secs(60),
    }
}

fn request(request_id: u64, command: Command, session_token: Option<String>) -> RequestEnvelope {
    RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id,
        session_token,
        command,
    }
}

#[tokio::test]
async fn local_signup_and_login_never_require_a_cloud_database() {
    let path = temp_db_path();
    let local_db = LocalDbContext::connect(&path)
        .await
        .expect("open local database");
    let config = config(path.clone());
    let state = KernelState::new(None, local_db, &config, Arc::new(Metrics::default()))
        .expect("create local-only kernel state");

    let signup = kinetib_kernel::ipc::dispatch::dispatch(
        &state,
        request(
            1,
            Command::SignUp {
                email: "private@example.test".into(),
                password: "local-password-1".into(),
                storage_mode: StorageMode::LocalOnly,
            },
            None,
        ),
    )
    .await;
    let session = match signup.result.expect("local signup succeeds") {
        ResponsePayload::Authenticated(session) => session,
        other => panic!("unexpected signup response: {other:?}"),
    };
    assert_eq!(session.storage_mode, StorageMode::LocalOnly);

    let status = kinetib_kernel::ipc::dispatch::dispatch(
        &state,
        request(
            2,
            Command::CheckSession,
            Some(session.session_token.clone()),
        ),
    )
    .await;
    match status.result.expect("session check succeeds") {
        ResponsePayload::SessionStatus(status) => {
            assert!(status.authenticated);
            assert_eq!(status.storage_mode, Some(StorageMode::LocalOnly));
        }
        other => panic!("unexpected session response: {other:?}"),
    }

    let transactions = kinetib_kernel::ipc::dispatch::dispatch(
        &state,
        request(
            3,
            Command::ListTransactions {
                page: 1,
                page_size: 10,
            },
            Some(session.session_token),
        ),
    )
    .await;
    match transactions
        .result
        .expect("LocalOnly transaction listing must succeed through SQLite")
    {
        ResponsePayload::Transactions(page) => {
            assert_eq!(page.page, 1);
            assert_eq!(page.page_size, 10);
            assert!(!page.has_more);
            assert!(
                page.items.is_empty(),
                "A newly created LocalOnly account should have no transactions"
            );
        }
        other => {
            panic!("Unexpected LocalOnly transaction response: {other:?}");
        }
    }

    drop(state);
    let _ = std::fs::remove_file(&path);
    let _ = std::fs::remove_file(path.with_extension("sqlite3-wal"));
    let _ = std::fs::remove_file(path.with_extension("sqlite3-shm"));
}

#[tokio::test]
async fn local_signin_uses_local_identity_namespace() {
    let path = temp_db_path();
    let local_db = LocalDbContext::connect(&path)
        .await
        .expect("open local database");
    let config = config(path.clone());
    let state = KernelState::new(None, local_db, &config, Arc::new(Metrics::default()))
        .expect("create local-only kernel state");

    let signup = kinetib_kernel::ipc::dispatch::dispatch(
        &state,
        request(
            1,
            Command::SignUp {
                email: "private@example.test".into(),
                password: "local-password-2".into(),
                storage_mode: StorageMode::LocalOnly,
            },
            None,
        ),
    )
    .await;
    signup.result.expect("local signup succeeds");

    let login = kinetib_kernel::ipc::dispatch::dispatch(
        &state,
        request(
            2,
            Command::LoginWithStorage {
                email: "PRIVATE@example.test".into(),
                password: "local-password-2".into(),
                storage_mode: StorageMode::LocalOnly,
            },
            None,
        ),
    )
    .await;
    let session = match login.result.expect("local login succeeds") {
        ResponsePayload::Authenticated(session) => session,
        other => panic!("unexpected login response: {other:?}"),
    };
    assert_eq!(session.storage_mode, StorageMode::LocalOnly);

    drop(state);
    let _ = std::fs::remove_file(&path);
    let _ = std::fs::remove_file(path.with_extension("sqlite3-wal"));
    let _ = std::fs::remove_file(path.with_extension("sqlite3-shm"));
}

#[tokio::test]
async fn feedback_rejects_unauthenticated_and_localonly_sessions() {
    let path = temp_db_path();
    let local_db = LocalDbContext::connect(&path)
        .await
        .expect("open local database");
    let config = config(path.clone());
    let state = KernelState::new(None, local_db, &config, Arc::new(Metrics::default()))
        .expect("create local-only kernel state");

    let submission = || {
        FeedbackSubmissionRequest::new(
            FeedbackCategory::BugReport,
            Some("Local feedback test".into()),
            "This report must never be sent to cloud storage.".into(),
            None,
            None,
            false,
            FeedbackPlatform::Linux,
            "0.5.2".into(),
            false,
            None,
        )
    };

    let anonymous = kinetib_kernel::ipc::dispatch::dispatch(
        &state,
        request(
            1,
            Command::SubmitFeedback {
                submission: submission(),
            },
            None,
        ),
    )
    .await;
    assert_eq!(
        anonymous.result.unwrap_err().code,
        ErrorCode::Unauthenticated
    );

    let signup = kinetib_kernel::ipc::dispatch::dispatch(
        &state,
        request(
            2,
            Command::SignUp {
                email: "feedback-local@example.test".into(),
                password: "local-feedback-password".into(),
                storage_mode: StorageMode::LocalOnly,
            },
            None,
        ),
    )
    .await;
    let session = match signup.result.expect("LocalOnly signup succeeds") {
        ResponsePayload::Authenticated(session) => session,
        other => panic!("unexpected signup response: {other:?}"),
    };

    let local_only = kinetib_kernel::ipc::dispatch::dispatch(
        &state,
        request(
            3,
            Command::SubmitFeedback {
                submission: submission(),
            },
            Some(session.session_token),
        ),
    )
    .await;
    assert_eq!(local_only.result.unwrap_err().code, ErrorCode::Forbidden);

    drop(state);
    let _ = std::fs::remove_file(&path);
    let _ = std::fs::remove_file(path.with_extension("sqlite3-wal"));
    let _ = std::fs::remove_file(path.with_extension("sqlite3-shm"));
}
