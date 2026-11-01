//! Browser IPC backend. A server-side WebSocket gateway is required at
//! `/ws/fintally-ipc`; that gateway must enforce the frame limit before sending
//! binary messages to the browser.

use super::{connection_should_be_dropped, map_client_error, BackendError, Operation};
use fintally_ipc_client::{
    client::IpcClient, config::ClientConfig, error::TransportError, transport::PayloadTransport,
    ClientError,
};
use fintally_ipc_protocol::{
    decode_frame_header, encode_frame_header, BudgetPage, BudgetRecord, CreateBudgetRequest,
    CreateTransactionRequest, DeleteBudgetRequest, DeleteBudgetResult, DeleteTransactionResult,
    FrameHeaderError, StorageMode, TransactionPage, TransactionReceiptData, TransactionRecord,
    UpdateBudgetRequest, UpdateTransactionRequest, FRAME_HEADER_BYTES, MAX_PAYLOAD_BYTES,
};
use futures_util::{SinkExt, StreamExt};
use gloo_net::websocket::{futures::WebSocket, Message};
use std::{cell::RefCell, future::Future, time::Duration};
use zeroize::Zeroizing;

pub struct UiBackend {
    client: RefCell<Option<IpcClient<BrowserTransport>>>,
}

impl UiBackend {
    pub fn new() -> Self {
        Self {
            client: RefCell::new(None),
        }
    }

    pub async fn login(
        &self,
        email: String,
        password: Zeroizing<String>,
        storage_mode: StorageMode,
    ) -> Result<(), BackendError> {
        self.client.borrow_mut().take();
        let transport = BrowserTransport::connect()?;
        let config = ClientConfig::with_timeouts(Duration::from_secs(2), Duration::from_secs(15))
            .map_err(|_| BackendError::RequestFailed)?;
        let mut client = IpcClient::new(transport, config);
        let result = client
            .sign_in(&email, password.as_str(), storage_mode)
            .await;
        match result {
            Ok(()) => {
                *self.client.borrow_mut() = Some(client);
                Ok(())
            }
            Err(error) => Err(map_client_error(error, Operation::Login)),
        }
    }

    /// Create an account in the selected store, then best-effort revoke the
    /// session returned by the current SignUp IPC contract. It is never retained.
    pub async fn sign_up(
        &self,
        email: String,
        password: Zeroizing<String>,
        storage_mode: StorageMode,
    ) -> Result<(), BackendError> {
        self.client.borrow_mut().take();
        if email.trim().is_empty()
            || email.trim() != email
            || email.len() > 320
            || password.len() < 8
            || password.len() > 72
        {
            return Err(BackendError::InvalidRequest);
        }

        let transport = BrowserTransport::connect()?;
        let config = ClientConfig::with_timeouts(Duration::from_secs(2), Duration::from_secs(15))
            .map_err(|_| BackendError::RequestFailed)?;
        let mut client = IpcClient::new(transport, config);
        let result = client
            .sign_up(&email, password.as_str(), storage_mode)
            .await;
        match result {
            Ok(()) => {
                let _ = client.logout().await;
                Ok(())
            }
            Err(error) => Err(map_client_error(error, Operation::SignUp)),
        }
    }

    /// Revoke the current server session when possible, and always drop the
    /// local client reference so an unusable token cannot be reused.
    pub async fn logout(&self) -> Result<(), BackendError> {
        let Some(mut client) = self.client.borrow_mut().take() else {
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
        let Some(mut client) = self.client.borrow_mut().take() else {
            return Err(BackendError::SessionRequired);
        };
        match client.list_transactions(page, page_size).await {
            Ok(page) => {
                *self.client.borrow_mut() = Some(client);
                Ok(page)
            }
            Err(error) => {
                if !connection_should_be_dropped(error) {
                    *self.client.borrow_mut() = Some(client);
                }
                Err(map_client_error(error, Operation::ListTransactions))
            }
        }
    }

    pub async fn create_transaction(
        &self,
        request: CreateTransactionRequest,
    ) -> Result<TransactionRecord, BackendError> {
        let Some(mut client) = self.client.borrow_mut().take() else {
            return Err(BackendError::SessionRequired);
        };
        match client.create_transaction(request).await {
            Ok(record) => {
                *self.client.borrow_mut() = Some(client);
                Ok(record)
            }
            Err(error) => {
                if !connection_should_be_dropped(error) {
                    *self.client.borrow_mut() = Some(client);
                }
                Err(map_client_error(error, Operation::CreateTransaction))
            }
        }
    }

    pub async fn update_transaction(
        &self,
        request: UpdateTransactionRequest,
    ) -> Result<TransactionRecord, BackendError> {
        let Some(mut client) = self.client.borrow_mut().take() else {
            return Err(BackendError::SessionRequired);
        };
        match client.update_transaction(request).await {
            Ok(record) => {
                *self.client.borrow_mut() = Some(client);
                Ok(record)
            }
            Err(error) => {
                if !connection_should_be_dropped(error) {
                    *self.client.borrow_mut() = Some(client);
                }
                Err(map_client_error(error, Operation::UpdateTransaction))
            }
        }
    }

    pub async fn delete_transaction(
        &self,
        transaction_id: i64,
    ) -> Result<DeleteTransactionResult, BackendError> {
        let Some(mut client) = self.client.borrow_mut().take() else {
            return Err(BackendError::SessionRequired);
        };
        match client.delete_transaction(transaction_id).await {
            Ok(result) => {
                *self.client.borrow_mut() = Some(client);
                Ok(result)
            }
            Err(error) => {
                if !connection_should_be_dropped(error) {
                    *self.client.borrow_mut() = Some(client);
                }
                Err(map_client_error(error, Operation::DeleteTransaction))
            }
        }
    }

    pub async fn list_budgets(&self) -> Result<BudgetPage, BackendError> {
        let Some(mut client) = self.client.borrow_mut().take() else {
            return Err(BackendError::SessionRequired);
        };
        match client.list_budgets().await {
            Ok(page) => {
                *self.client.borrow_mut() = Some(client);
                Ok(page)
            }
            Err(error) => {
                if !connection_should_be_dropped(error) {
                    *self.client.borrow_mut() = Some(client);
                }
                Err(map_client_error(error, Operation::ListBudgets))
            }
        }
    }

    pub async fn create_budget(
        &self,
        request: CreateBudgetRequest,
    ) -> Result<BudgetRecord, BackendError> {
        let Some(mut client) = self.client.borrow_mut().take() else {
            return Err(BackendError::SessionRequired);
        };
        match client.create_budget(request).await {
            Ok(record) => {
                *self.client.borrow_mut() = Some(client);
                Ok(record)
            }
            Err(error) => {
                if !connection_should_be_dropped(error) {
                    *self.client.borrow_mut() = Some(client);
                }
                Err(map_client_error(error, Operation::CreateBudget))
            }
        }
    }

    pub async fn update_budget(
        &self,
        request: UpdateBudgetRequest,
    ) -> Result<BudgetRecord, BackendError> {
        let Some(mut client) = self.client.borrow_mut().take() else {
            return Err(BackendError::SessionRequired);
        };
        match client.update_budget(request).await {
            Ok(record) => {
                *self.client.borrow_mut() = Some(client);
                Ok(record)
            }
            Err(error) => {
                if !connection_should_be_dropped(error) {
                    *self.client.borrow_mut() = Some(client);
                }
                Err(map_client_error(error, Operation::UpdateBudget))
            }
        }
    }

    pub async fn delete_budget(
        &self,
        request: DeleteBudgetRequest,
    ) -> Result<DeleteBudgetResult, BackendError> {
        let Some(mut client) = self.client.borrow_mut().take() else {
            return Err(BackendError::SessionRequired);
        };
        match client.delete_budget(request).await {
            Ok(result) => {
                *self.client.borrow_mut() = Some(client);
                Ok(result)
            }
            Err(error) => {
                if !connection_should_be_dropped(error) {
                    *self.client.borrow_mut() = Some(client);
                }
                Err(map_client_error(error, Operation::DeleteBudget))
            }
        }
    }

    pub async fn get_transaction_receipt(
        &self,
        transaction_id: i64,
    ) -> Result<TransactionReceiptData, BackendError> {
        let Some(mut client) = self.client.borrow_mut().take() else {
            return Err(BackendError::SessionRequired);
        };
        match client.get_transaction_receipt(transaction_id).await {
            Ok(receipt) => {
                *self.client.borrow_mut() = Some(client);
                Ok(receipt)
            }
            Err(error) => {
                if !connection_should_be_dropped(error) {
                    *self.client.borrow_mut() = Some(client);
                }
                Err(map_client_error(error, Operation::GetTransactionReceipt))
            }
        }
    }

    /// Drops local client state without making a remote request. Prefer `logout`
    /// when the connection is usable so the server session is revoked immediately.
    pub async fn clear_local_session(&self) {
        self.client.borrow_mut().take();
    }
}

impl Default for UiBackend {
    fn default() -> Self {
        Self::new()
    }
}

pub struct BrowserTransport {
    socket: WebSocket,
}

impl BrowserTransport {
    fn connect() -> Result<Self, BackendError> {
        let window = web_sys::window().ok_or(BackendError::Unavailable)?;
        let location = window.location();
        let protocol = location.protocol().map_err(|_| BackendError::Unavailable)?;
        let host = location.host().map_err(|_| BackendError::Unavailable)?;
        if host.is_empty() {
            return Err(BackendError::Unavailable);
        }
        let scheme = websocket_scheme(&protocol);
        let url = format!("{scheme}://{host}/ws/fintally-ipc");
        let socket = WebSocket::open(&url).map_err(|_| BackendError::Unavailable)?;
        Ok(Self { socket })
    }
}

impl PayloadTransport for BrowserTransport {
    fn exchange_payload<'a>(
        &'a mut self,
        payload: &'a [u8],
    ) -> impl Future<Output = Result<Vec<u8>, TransportError>> + 'a {
        async move {
            if payload.len() > MAX_PAYLOAD_BYTES {
                return Err(TransportError::PayloadTooLarge);
            }

            let header = encode_frame_header(payload.len()).map_err(map_header_error)?;
            let frame_len = FRAME_HEADER_BYTES
                .checked_add(payload.len())
                .ok_or(TransportError::PayloadTooLarge)?;
            let mut frame = Vec::new();
            frame
                .try_reserve_exact(frame_len)
                .map_err(|_| TransportError::AllocationFailed)?;
            frame.extend_from_slice(&header);
            frame.extend_from_slice(payload);

            self.socket
                .send(Message::Bytes(frame))
                .await
                .map_err(|_| TransportError::Io)?;

            let message = self
                .socket
                .next()
                .await
                .ok_or(TransportError::Closed)?
                .map_err(|_| TransportError::Io)?;
            let Message::Bytes(mut frame) = message else {
                return Err(TransportError::InvalidFrameHeader);
            };

            let max_frame_len = FRAME_HEADER_BYTES
                .checked_add(MAX_PAYLOAD_BYTES)
                .ok_or(TransportError::PayloadTooLarge)?;
            if frame.len() > max_frame_len {
                return Err(TransportError::PayloadTooLarge);
            }
            if frame.len() < FRAME_HEADER_BYTES {
                return Err(TransportError::InvalidFrameHeader);
            }

            let payload_len = decode_frame_header(&frame[..FRAME_HEADER_BYTES])
                .map_err(|_| TransportError::InvalidFrameHeader)?;
            if payload_len > MAX_PAYLOAD_BYTES {
                return Err(TransportError::PayloadTooLarge);
            }
            if frame.len() != FRAME_HEADER_BYTES + payload_len {
                return Err(TransportError::InvalidFrameLength);
            }

            frame.drain(..FRAME_HEADER_BYTES);
            Ok(frame)
        }
    }
}

fn map_header_error(_error: FrameHeaderError) -> TransportError {
    TransportError::InvalidFrameHeader
}

fn websocket_scheme(page_protocol: &str) -> &'static str {
    if page_protocol.eq_ignore_ascii_case("https:") {
        "wss"
    } else {
        "ws"
    }
}

#[cfg(test)]
mod tests {
    use super::websocket_scheme;

    #[test]
    fn https_pages_use_secure_websockets() {
        assert_eq!(websocket_scheme("https:"), "wss");
    }

    #[test]
    fn http_pages_use_websockets_for_local_development() {
        assert_eq!(websocket_scheme("http:"), "ws");
    }
}
