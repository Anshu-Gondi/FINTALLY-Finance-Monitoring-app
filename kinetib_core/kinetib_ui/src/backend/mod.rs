//! Platform-specific transport adapters for the Slint UI.

#[cfg(target_arch = "wasm32")]
mod browser;
#[cfg(not(target_arch = "wasm32"))]
mod native;

#[cfg(target_arch = "wasm32")]
pub use browser::UiBackend;
#[cfg(not(target_arch = "wasm32"))]
pub use native::UiBackend;

use kinetib_ipc_client::ClientError;
use kinetib_ipc_protocol::ErrorCode;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Operation {
    Login,
    SignUp,
    Logout,
    ListTransactions,
    CreateTransaction,
    UpdateTransaction,
    DeleteTransaction,
    GetTransactionReceipt,
    ListBudgets,
    CreateBudget,
    UpdateBudget,
    DeleteBudget,
    SubmitFeedback,
}

/// Safe, non-sensitive errors suitable for display in the UI.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum BackendError {
    InvalidCredentials,
    AccountCouldNotBeCreated,
    SessionRequired,
    SessionExpired,
    CloudSyncRequired,
    Busy,
    Unavailable,
    TimedOut,
    OutcomeUnknown,
    IncompatibleProtocol,
    InvalidRequest,
    Conflict,
    RequestFailed,
}

impl BackendError {
    pub(crate) const fn user_message(self) -> &'static str {
        match self {
            Self::InvalidCredentials => "Sign-in failed. Check your email and password.",
            Self::AccountCouldNotBeCreated => {
                "The account could not be created. Try signing in if you already have an account."
            }
            Self::SessionRequired => "Sign in to continue.",
            Self::SessionExpired => "Your session expired. Please sign in again.",
            Self::CloudSyncRequired => "CloudSync is required to submit feedback. LocalOnly data stays on this device.",
            Self::Busy => "The service is busy. Try again shortly.",
            Self::Unavailable => {
                "The FinTally connection was interrupted. Sign in again with the same account to retry safely."
            }
            Self::TimedOut => "The request timed out and its result is unknown. Sign in again with the same account to retry safely.",
            Self::OutcomeUnknown => "The server could not confirm the result. Retry the unchanged submission using the same reference.",
            Self::IncompatibleProtocol => "The client and kernel protocol versions do not match. Update both sides before retrying.",
            Self::InvalidRequest => "The request was rejected. Check input lengths and values.",
            Self::Conflict => "This submission conflicts with an earlier request. Start a new submission or retry the original unchanged request.",
            Self::RequestFailed => "The request failed. Sensitive diagnostics were not exposed.",
        }
    }
}

pub(crate) fn map_client_error(error: ClientError, operation: Operation) -> BackendError {
    match error {
        ClientError::ConnectTimeout | ClientError::ConnectFailed => BackendError::Unavailable,
        ClientError::RequestTimeout => BackendError::TimedOut,
        ClientError::NotAuthenticated => BackendError::SessionRequired,
        ClientError::IncompatibleProtocolVersion
        | ClientError::RequestIdMismatch
        | ClientError::MalformedResponse
        | ClientError::InvalidFrame
        | ClientError::PayloadTooLarge
        | ClientError::UnexpectedResponse
        | ClientError::ConnectionUnusable
        | ClientError::RequestIdExhausted => BackendError::IncompatibleProtocol,
        ClientError::InvalidAuthenticationResponse => BackendError::RequestFailed,
        ClientError::SessionModeMismatch => BackendError::SessionRequired,
        ClientError::InvalidRequest => BackendError::InvalidRequest,
        ClientError::EncodeFailed => BackendError::RequestFailed,
        ClientError::Remote(ErrorCode::Busy) => BackendError::Busy,
        ClientError::Remote(ErrorCode::Unauthenticated) => match operation {
            Operation::Login => BackendError::InvalidCredentials,
            Operation::SignUp => BackendError::AccountCouldNotBeCreated,
            Operation::Logout => BackendError::SessionRequired,
            Operation::ListTransactions
            | Operation::CreateTransaction
            | Operation::UpdateTransaction
            | Operation::DeleteTransaction
            | Operation::GetTransactionReceipt
            | Operation::ListBudgets
            | Operation::CreateBudget
            | Operation::UpdateBudget
            | Operation::DeleteBudget
            | Operation::SubmitFeedback => BackendError::SessionExpired,
        },
        ClientError::Remote(ErrorCode::Conflict) if operation == Operation::SignUp => {
            BackendError::AccountCouldNotBeCreated
        }
        ClientError::Remote(ErrorCode::Conflict)
            if matches!(
                operation,
                Operation::CreateBudget | Operation::UpdateBudget | Operation::DeleteBudget
            ) =>
        {
            BackendError::Conflict
        }
        ClientError::Remote(ErrorCode::Conflict) if operation == Operation::SubmitFeedback => {
            BackendError::Conflict
        }
        ClientError::Remote(ErrorCode::InvalidRequest) if operation == Operation::Login => {
            BackendError::InvalidCredentials
        }
        ClientError::Remote(ErrorCode::InvalidRequest) if operation == Operation::SignUp => {
            BackendError::InvalidRequest
        }
        ClientError::Remote(ErrorCode::UnsupportedProtocolVersion) => {
            BackendError::IncompatibleProtocol
        }
        ClientError::Remote(ErrorCode::Internal) if operation == Operation::SubmitFeedback => {
            BackendError::OutcomeUnknown
        }
        ClientError::Transport => BackendError::Unavailable,
        ClientError::Remote(ErrorCode::Forbidden) if operation == Operation::SubmitFeedback => {
            BackendError::CloudSyncRequired
        }
        ClientError::Remote(
            ErrorCode::Forbidden | ErrorCode::NotFound | ErrorCode::Conflict | ErrorCode::Internal,
        )
        | ClientError::Remote(ErrorCode::InvalidRequest) => BackendError::RequestFailed,
    }
}

pub(crate) fn connection_should_be_dropped(error: ClientError) -> bool {
    matches!(
        error,
        ClientError::RequestTimeout
            | ClientError::Transport
            | ClientError::InvalidFrame
            | ClientError::PayloadTooLarge
            | ClientError::MalformedResponse
            | ClientError::IncompatibleProtocolVersion
            | ClientError::RequestIdMismatch
            | ClientError::ConnectionUnusable
            | ClientError::RequestIdExhausted
            | ClientError::UnexpectedResponse
            | ClientError::InvalidAuthenticationResponse
            | ClientError::SessionModeMismatch
            | ClientError::Remote(ErrorCode::Unauthenticated)
    )
}
