//! Native desktop/mobile IPC backend.

use super::{connection_should_be_dropped, map_client_error, BackendError, Operation};
use kinetib_ipc_client::{
    client::IpcClient, config::ClientConfig, transport::FramedConnection, ClientError,
};
use kinetib_ipc_protocol::{
    BudgetPage, BudgetRecord, CreateBudgetRequest, CreateTransactionRequest, DeleteBudgetRequest,
    DeleteBudgetResult, DeleteTransactionResult, FeedbackReceipt, FeedbackSubmissionRequest,
    StorageMode, TransactionPage, TransactionReceiptData, TransactionRecord, UpdateBudgetRequest,
    UpdateTransactionRequest,
};
#[cfg(unix)]
use std::path::PathBuf;
use std::time::Duration;
use tokio::sync::Mutex;
use zeroize::Zeroizing;

#[cfg(unix)]
type PlatformClient = IpcClient<FramedConnection<tokio::net::UnixStream>>;
#[cfg(windows)]
type PlatformClient = IpcClient<FramedConnection<tokio::net::windows::named_pipe::NamedPipeClient>>;

#[cfg(unix)]
enum ConnectedClient {
    Unix(PlatformClient),
}
#[cfg(windows)]
enum ConnectedClient {
    Windows(PlatformClient),
}

impl ConnectedClient {
    async fn login(
        &mut self,
        email: &str,
        password: &str,
        storage_mode: StorageMode,
    ) -> Result<(), ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.sign_in(email, password, storage_mode).await,
            #[cfg(windows)]
            Self::Windows(client) => client.sign_in(email, password, storage_mode).await,
        }
    }

    async fn sign_up(
        &mut self,
        email: &str,
        password: &str,
        storage_mode: StorageMode,
    ) -> Result<(), ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.sign_up(email, password, storage_mode).await,
            #[cfg(windows)]
            Self::Windows(client) => client.sign_up(email, password, storage_mode).await,
        }
    }

    async fn logout(&mut self) -> Result<bool, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.logout().await,
            #[cfg(windows)]
            Self::Windows(client) => client.logout().await,
        }
    }

    async fn list_transactions(
        &mut self,
        page: u32,
        page_size: u16,
    ) -> Result<TransactionPage, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.list_transactions(page, page_size).await,
            #[cfg(windows)]
            Self::Windows(client) => client.list_transactions(page, page_size).await,
        }
    }

    async fn create_transaction(
        &mut self,
        request: CreateTransactionRequest,
    ) -> Result<TransactionRecord, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.create_transaction(request).await,
            #[cfg(windows)]
            Self::Windows(client) => client.create_transaction(request).await,
        }
    }

    async fn update_transaction(
        &mut self,
        request: UpdateTransactionRequest,
    ) -> Result<TransactionRecord, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.update_transaction(request).await,
            #[cfg(windows)]
            Self::Windows(client) => client.update_transaction(request).await,
        }
    }

    async fn delete_transaction(
        &mut self,
        transaction_id: i64,
    ) -> Result<DeleteTransactionResult, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.delete_transaction(transaction_id).await,
            #[cfg(windows)]
            Self::Windows(client) => client.delete_transaction(transaction_id).await,
        }
    }

    async fn list_budgets(&mut self) -> Result<BudgetPage, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.list_budgets().await,
            #[cfg(windows)]
            Self::Windows(client) => client.list_budgets().await,
        }
    }

    async fn create_budget(
        &mut self,
        request: CreateBudgetRequest,
    ) -> Result<BudgetRecord, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.create_budget(request).await,
            #[cfg(windows)]
            Self::Windows(client) => client.create_budget(request).await,
        }
    }

    async fn update_budget(
        &mut self,
        request: UpdateBudgetRequest,
    ) -> Result<BudgetRecord, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.update_budget(request).await,
            #[cfg(windows)]
            Self::Windows(client) => client.update_budget(request).await,
        }
    }

    async fn delete_budget(
        &mut self,
        request: DeleteBudgetRequest,
    ) -> Result<DeleteBudgetResult, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.delete_budget(request).await,
            #[cfg(windows)]
            Self::Windows(client) => client.delete_budget(request).await,
        }
    }

    async fn get_transaction_receipt(
        &mut self,
        transaction_id: i64,
    ) -> Result<TransactionReceiptData, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.get_transaction_receipt(transaction_id).await,
            #[cfg(windows)]
            Self::Windows(client) => client.get_transaction_receipt(transaction_id).await,
        }
    }

    async fn submit_feedback(
        &mut self,
        request: FeedbackSubmissionRequest,
    ) -> Result<FeedbackReceipt, ClientError> {
        match self {
            #[cfg(unix)]
            Self::Unix(client) => client.submit_feedback(request).await,
            #[cfg(windows)]
            Self::Windows(client) => client.submit_feedback(request).await,
        }
    }
}

pub struct UiBackend {
    client: Mutex<Option<ConnectedClient>>,
}

impl UiBackend {
    pub fn new() -> Self {
        Self {
            client: Mutex::new(None),
        }
    }

    pub async fn login(
        &self,
        email: String,
        password: Zeroizing<String>,
        storage_mode: StorageMode,
    ) -> Result<(), BackendError> {
        // Drop any previous user's session before authenticating a new identity.
        self.client.lock().await.take();
        let mut client = connect().await?;
        match client.login(&email, password.as_str(), storage_mode).await {
            Ok(()) => {
                *self.client.lock().await = Some(client);
                Ok(())
            }
            Err(error) => Err(map_client_error(error, Operation::Login)),
        }
    }

    /// Create the account in the explicitly selected store, then best-effort
    /// revoke the session that the current SignUp IPC response also issues.
    /// The signup client is never retained as an authenticated UI session.
    pub async fn sign_up(
        &self,
        email: String,
        password: Zeroizing<String>,
        storage_mode: StorageMode,
    ) -> Result<(), BackendError> {
        self.client.lock().await.take();
        if email.trim().is_empty()
            || email.trim() != email
            || email.len() > 320
            || password.len() < 8
            || password.len() > 72
        {
            return Err(BackendError::InvalidRequest);
        }

        let mut client = connect().await?;
        match client
            .sign_up(&email, password.as_str(), storage_mode)
            .await
        {
            Ok(()) => {
                // If logout fails, dropping this client still clears its local token;
                // any server-side session remains bounded by the kernel's TTL.
                let _ = client.logout().await;
                Ok(())
            }
            Err(error) => Err(map_client_error(error, Operation::SignUp)),
        }
    }

    /// Revoke the current session when the transport is still usable.
    /// Taking the client first prevents its mutex from being held across I/O.
    pub async fn logout(&self) -> Result<(), BackendError> {
        let Some(mut client) = self.client.lock().await.take() else {
            return Ok(());
        };
        client
            .logout()
            .await
            .map(|_| ())
            .map_err(|error| map_client_error(error, Operation::Logout))
    }

    pub async fn list_transactions(
        &self,
        page: u32,
        page_size: u16,
    ) -> Result<TransactionPage, BackendError> {
        let mut slot = self.client.lock().await;
        let Some(client) = slot.as_mut() else {
            return Err(BackendError::SessionRequired);
        };
        match client.list_transactions(page, page_size).await {
            Ok(page) => Ok(page),
            Err(error) => {
                if connection_should_be_dropped(error) {
                    *slot = None;
                }
                Err(map_client_error(error, Operation::ListTransactions))
            }
        }
    }

    pub async fn create_transaction(
        &self,
        request: CreateTransactionRequest,
    ) -> Result<TransactionRecord, BackendError> {
        let mut slot = self.client.lock().await;
        let Some(client) = slot.as_mut() else {
            return Err(BackendError::SessionRequired);
        };
        match client.create_transaction(request).await {
            Ok(record) => Ok(record),
            Err(error) => {
                if connection_should_be_dropped(error) {
                    *slot = None;
                }
                Err(map_client_error(error, Operation::CreateTransaction))
            }
        }
    }

    pub async fn update_transaction(
        &self,
        request: UpdateTransactionRequest,
    ) -> Result<TransactionRecord, BackendError> {
        let mut slot = self.client.lock().await;
        let Some(client) = slot.as_mut() else {
            return Err(BackendError::SessionRequired);
        };
        match client.update_transaction(request).await {
            Ok(record) => Ok(record),
            Err(error) => {
                if connection_should_be_dropped(error) {
                    *slot = None;
                }
                Err(map_client_error(error, Operation::UpdateTransaction))
            }
        }
    }

    pub async fn delete_transaction(
        &self,
        transaction_id: i64,
    ) -> Result<DeleteTransactionResult, BackendError> {
        let mut slot = self.client.lock().await;
        let Some(client) = slot.as_mut() else {
            return Err(BackendError::SessionRequired);
        };
        match client.delete_transaction(transaction_id).await {
            Ok(result) => Ok(result),
            Err(error) => {
                if connection_should_be_dropped(error) {
                    *slot = None;
                }
                Err(map_client_error(error, Operation::DeleteTransaction))
            }
        }
    }

    pub async fn list_budgets(&self) -> Result<BudgetPage, BackendError> {
        let mut slot = self.client.lock().await;
        let Some(client) = slot.as_mut() else {
            return Err(BackendError::SessionRequired);
        };
        match client.list_budgets().await {
            Ok(page) => Ok(page),
            Err(error) => {
                if connection_should_be_dropped(error) {
                    *slot = None;
                }
                Err(map_client_error(error, Operation::ListBudgets))
            }
        }
    }

    pub async fn create_budget(
        &self,
        request: CreateBudgetRequest,
    ) -> Result<BudgetRecord, BackendError> {
        let mut slot = self.client.lock().await;
        let Some(client) = slot.as_mut() else {
            return Err(BackendError::SessionRequired);
        };
        match client.create_budget(request).await {
            Ok(record) => Ok(record),
            Err(error) => {
                if connection_should_be_dropped(error) {
                    *slot = None;
                }
                Err(map_client_error(error, Operation::CreateBudget))
            }
        }
    }

    pub async fn update_budget(
        &self,
        request: UpdateBudgetRequest,
    ) -> Result<BudgetRecord, BackendError> {
        let mut slot = self.client.lock().await;
        let Some(client) = slot.as_mut() else {
            return Err(BackendError::SessionRequired);
        };
        match client.update_budget(request).await {
            Ok(record) => Ok(record),
            Err(error) => {
                if connection_should_be_dropped(error) {
                    *slot = None;
                }
                Err(map_client_error(error, Operation::UpdateBudget))
            }
        }
    }

    pub async fn delete_budget(
        &self,
        request: DeleteBudgetRequest,
    ) -> Result<DeleteBudgetResult, BackendError> {
        let mut slot = self.client.lock().await;
        let Some(client) = slot.as_mut() else {
            return Err(BackendError::SessionRequired);
        };
        match client.delete_budget(request).await {
            Ok(result) => Ok(result),
            Err(error) => {
                if connection_should_be_dropped(error) {
                    *slot = None;
                }
                Err(map_client_error(error, Operation::DeleteBudget))
            }
        }
    }

    pub async fn get_transaction_receipt(
        &self,
        transaction_id: i64,
    ) -> Result<TransactionReceiptData, BackendError> {
        let mut slot = self.client.lock().await;
        let Some(client) = slot.as_mut() else {
            return Err(BackendError::SessionRequired);
        };
        match client.get_transaction_receipt(transaction_id).await {
            Ok(receipt) => Ok(receipt),
            Err(error) => {
                if connection_should_be_dropped(error) {
                    *slot = None;
                }
                Err(map_client_error(error, Operation::GetTransactionReceipt))
            }
        }
    }

    pub async fn submit_feedback(
        &self,
        request: FeedbackSubmissionRequest,
    ) -> Result<FeedbackReceipt, BackendError> {
        let mut slot = self.client.lock().await;
        let Some(client) = slot.as_mut() else {
            return Err(BackendError::SessionRequired);
        };
        match client.submit_feedback(request).await {
            Ok(receipt) => Ok(receipt),
            Err(error) => {
                if connection_should_be_dropped(error) {
                    *slot = None;
                }
                Err(map_client_error(error, Operation::SubmitFeedback))
            }
        }
    }

    /// Drops local client state without a remote request. Prefer `logout`
    /// while the connection is healthy so the server session is revoked.
    pub async fn clear_local_session(&self) {
        self.client.lock().await.take();
    }
}

impl Default for UiBackend {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(unix)]
async fn connect() -> Result<ConnectedClient, BackendError> {
    use kinetib_ipc_client::transport::local_socket::connect_unix;

    let path = socket_path()?;
    let config = ClientConfig::with_timeouts(Duration::from_secs(2), Duration::from_secs(15))
        .map_err(|_| BackendError::RequestFailed)?;
    connect_unix(path, config)
        .await
        .map(ConnectedClient::Unix)
        .map_err(|error| map_client_error(error, Operation::Login))
}

#[cfg(unix)]
fn socket_path() -> Result<PathBuf, BackendError> {
    if let Some(path) = std::env::var_os("KINETIB_IPC_SOCKET_PATH") {
        if path.is_empty() {
            return Err(BackendError::Unavailable);
        }
        return Ok(PathBuf::from(path));
    }

    #[cfg(any(target_os = "android", target_os = "ios"))]
    {
        // Mobile packaging must explicitly choose a private app-container path.
        return Err(BackendError::Unavailable);
    }

    #[cfg(not(any(target_os = "android", target_os = "ios")))]
    {
        Ok(PathBuf::from("/tmp/kinetib-kernel.sock"))
    }
}

#[cfg(windows)]
async fn connect() -> Result<ConnectedClient, BackendError> {
    use kinetib_ipc_client::transport::local_socket::connect_named_pipe;

    let pipe = std::env::var("KINETIB_IPC_PIPE_NAME")
        .unwrap_or_else(|_| r"\\.\pipe\kinetib-kernel".to_owned());
    let config = ClientConfig::with_timeouts(Duration::from_secs(2), Duration::from_secs(15))
        .map_err(|_| BackendError::RequestFailed)?;
    connect_named_pipe(&pipe, config)
        .await
        .map(ConnectedClient::Windows)
        .map_err(|error| map_client_error(error, Operation::Login))
}

#[cfg(not(any(unix, windows)))]
async fn connect() -> Result<ConnectedClient, BackendError> {
    Err(BackendError::Unavailable)
}
