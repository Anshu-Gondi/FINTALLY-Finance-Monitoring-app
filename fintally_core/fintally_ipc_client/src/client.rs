//! Request-correlated IPC client with bounded payloads, deadlines, and auth APIs.
//!
//! This crate transports authentication intent. It does not verify Google tokens,
//! choose a database, or grant permissions; those remain kernel/application duties.

use crate::config::ClientConfig;
use crate::error::{ClientError, TransportError};
use crate::timeout::with_timeout;
use crate::transport::PayloadTransport;
use fintally_ipc_protocol::{
    decode_payload, encode_payload, BudgetPage, BudgetRecord, Command, CreateBudgetRequest,
    CreateTransactionRequest, DeleteBudgetRequest, DeleteBudgetResult, DeleteTransactionResult,
    ErrorCode, FieldChange, IdentityProvider, LoginResult, RequestEnvelope, ResponseEnvelope,
    ResponsePayload, SessionStatus, StorageMode, TransactionPage, TransactionReceiptData,
    TransactionRecord, UpdateBudgetRequest, UpdateTransactionRequest, IPC_PROTOCOL_VERSION,
    MAX_PAYLOAD_BYTES,
};
use std::time::Duration;
use zeroize::{Zeroize, Zeroizing};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SessionMetadata {
    pub storage_mode: StorageMode,
    pub provider: IdentityProvider,
    pub expires_at_unix_seconds: Option<u64>,
}

pub struct IpcClient<T> {
    transport: T,
    config: ClientConfig,
    next_request_id: Option<u64>,
    session_token: Option<Zeroizing<String>>,
    session_metadata: Option<SessionMetadata>,
    unusable: bool,
}

impl<T: PayloadTransport> IpcClient<T> {
    pub fn new(transport: T, config: ClientConfig) -> Self {
        Self {
            transport,
            config,
            next_request_id: Some(1),
            session_token: None,
            session_metadata: None,
            unusable: false,
        }
    }

    pub fn has_session(&self) -> bool {
        self.session_token.is_some()
    }

    /// Metadata is a UI hint only. Server-side session state remains authoritative.
    pub fn session_metadata(&self) -> Option<SessionMetadata> {
        self.session_metadata
    }

    /// Clears this client's in-memory token only. Use `logout()` to request server
    /// revocation as well. Tokens should not be persisted in ordinary UI settings.
    pub fn clear_local_session(&mut self) {
        self.session_token = None;
        self.session_metadata = None;
    }

    /// Legacy login method retained for compatibility with the current kernel.
    /// New UI pages should call `sign_in` with an explicit storage choice.
    pub async fn login(&mut self, email: &str, password: &str) -> Result<(), ClientError> {
        self.clear_local_session();
        let response = self
            .round_trip(
                Command::Login {
                    email: email.to_owned(),
                    password: password.to_owned(),
                },
                None,
            )
            .await?;

        match response {
            ResponsePayload::Login(LoginResult { session_token }) => {
                if session_token.is_empty() {
                    self.unusable = true;
                    return Err(ClientError::MalformedResponse);
                }
                self.session_token = Some(Zeroizing::new(session_token));
                Ok(())
            }
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    /// Create an email/password account using an explicit storage policy.
    /// `LocalOnly` must be implemented by the kernel as a local-store-only path.
    pub async fn sign_up(
        &mut self,
        email: &str,
        password: &str,
        storage_mode: StorageMode,
    ) -> Result<(), ClientError> {
        self.authenticate(
            Command::SignUp {
                email: email.to_owned(),
                password: password.to_owned(),
                storage_mode,
            },
            storage_mode,
            IdentityProvider::EmailPassword,
        )
        .await
    }

    /// Sign in with email/password in the explicitly selected storage scope.
    pub async fn sign_in(
        &mut self,
        email: &str,
        password: &str,
        storage_mode: StorageMode,
    ) -> Result<(), ClientError> {
        self.authenticate(
            Command::LoginWithStorage {
                email: email.to_owned(),
                password: password.to_owned(),
                storage_mode,
            },
            storage_mode,
            IdentityProvider::EmailPassword,
        )
        .await
    }

    /// Register or link an account using a Google ID token. The kernel must
    /// verify signature, issuer, audience, and expiry and enforce signup policy.
    /// This client must never contain a Google client secret.
    pub async fn sign_up_with_google(
        &mut self,
        id_token: &str,
        storage_mode: StorageMode,
    ) -> Result<(), ClientError> {
        self.authenticate(
            Command::GoogleSignUp {
                id_token: id_token.to_owned(),
                storage_mode,
            },
            storage_mode,
            IdentityProvider::Google,
        )
        .await
    }

    /// Authenticate using a Google ID token obtained by the platform identity
    /// adapter. The kernel must verify signature, issuer, audience, and expiry.
    /// This client must never contain a Google client secret.
    pub async fn sign_in_with_google(
        &mut self,
        id_token: &str,
        storage_mode: StorageMode,
    ) -> Result<(), ClientError> {
        self.authenticate(
            Command::GoogleSignIn {
                id_token: id_token.to_owned(),
                storage_mode,
            },
            storage_mode,
            IdentityProvider::Google,
        )
        .await
    }

    async fn authenticate(
        &mut self,
        command: Command,
        expected_mode: StorageMode,
        expected_provider: IdentityProvider,
    ) -> Result<(), ClientError> {
        // Never keep the previous user's token after a failed account switch.
        self.clear_local_session();

        let response = self.round_trip(command, None).await?;
        match response {
            ResponsePayload::Authenticated(mut session) => {
                if session.session_token.is_empty() {
                    session.session_token.zeroize();
                    self.unusable = true;
                    return Err(ClientError::MalformedResponse);
                }
                if session.storage_mode != expected_mode || session.provider != expected_provider {
                    session.session_token.zeroize();
                    self.unusable = true;
                    return Err(ClientError::SessionModeMismatch);
                }

                self.session_metadata = Some(SessionMetadata {
                    storage_mode: session.storage_mode,
                    provider: session.provider,
                    expires_at_unix_seconds: session.expires_at_unix_seconds,
                });
                self.session_token = Some(Zeroizing::new(session.session_token));
                Ok(())
            }
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    /// Validate the current session with the kernel instead of trusting local state.
    pub async fn check_session(&mut self) -> Result<SessionStatus, ClientError> {
        let token = self
            .session_token
            .as_ref()
            .ok_or(ClientError::NotAuthenticated)?
            .as_str()
            .to_owned();

        let response = self.round_trip(Command::CheckSession, Some(token)).await;
        let payload = match response {
            Ok(payload) => payload,
            Err(error) => {
                if matches!(error, ClientError::Remote(ErrorCode::Unauthenticated)) {
                    self.clear_local_session();
                }
                return Err(error);
            }
        };

        match payload {
            ResponsePayload::SessionStatus(status) if status.is_well_formed() => {
                if status.authenticated {
                    if let (Some(storage_mode), Some(provider)) =
                        (status.storage_mode, status.provider)
                    {
                        self.session_metadata = Some(SessionMetadata {
                            storage_mode,
                            provider,
                            expires_at_unix_seconds: status.expires_at_unix_seconds,
                        });
                    } else {
                        self.clear_local_session();
                        self.unusable = true;
                        return Err(ClientError::MalformedResponse);
                    }
                } else {
                    self.clear_local_session();
                }
                Ok(status)
            }
            mut other => {
                zeroize_payload(&mut other);
                self.clear_local_session();
                self.unusable = true;
                Err(ClientError::MalformedResponse)
            }
        }
    }

    /// Request server-side revocation. The local bearer token is erased whether
    /// the remote operation succeeds or fails, preventing accidental reuse.
    pub async fn logout(&mut self) -> Result<bool, ClientError> {
        let token = self
            .session_token
            .as_ref()
            .ok_or(ClientError::NotAuthenticated)?
            .as_str()
            .to_owned();

        let response = self.round_trip(Command::Logout, Some(token)).await;
        self.clear_local_session();

        match response? {
            ResponsePayload::LoggedOut(result) => Ok(result.revoked),
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    pub async fn list_transactions(
        &mut self,
        page: u32,
        page_size: u16,
    ) -> Result<TransactionPage, ClientError> {
        let token = self
            .session_token
            .as_ref()
            .ok_or(ClientError::NotAuthenticated)?
            .as_str()
            .to_owned();

        let response = self
            .round_trip(Command::ListTransactions { page, page_size }, Some(token))
            .await;
        let payload = match response {
            Ok(payload) => payload,
            Err(error) => {
                if matches!(error, ClientError::Remote(ErrorCode::Unauthenticated)) {
                    self.clear_local_session();
                }
                return Err(error);
            }
        };
        match payload {
            ResponsePayload::Transactions(page) => Ok(page),
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    /// Create a transaction in the current authenticated storage scope.
    pub async fn create_transaction(
        &mut self,
        transaction: CreateTransactionRequest,
    ) -> Result<TransactionRecord, ClientError> {
        let token = self.current_token()?;
        let response = self
            .round_trip(Command::CreateTransaction { transaction }, Some(token))
            .await;
        let payload = self.clear_session_on_auth_failure(response)?;
        match payload {
            ResponsePayload::TransactionCreated(record) => Ok(record),
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    /// Apply explicit field changes. Nullable fields use `Clear`; omission means
    /// unchanged. A successful response contains the canonical stored record.
    pub async fn update_transaction(
        &mut self,
        transaction: UpdateTransactionRequest,
    ) -> Result<TransactionRecord, ClientError> {
        let token = self.current_token()?;
        let response = self
            .round_trip(Command::UpdateTransaction { transaction }, Some(token))
            .await;
        let payload = self.clear_session_on_auth_failure(response)?;
        match payload {
            ResponsePayload::TransactionUpdated(record) => Ok(record),
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    /// Delete a transaction owned by the current session principal.
    pub async fn delete_transaction(
        &mut self,
        transaction_id: i64,
    ) -> Result<DeleteTransactionResult, ClientError> {
        let token = self.current_token()?;
        let response = self
            .round_trip(Command::DeleteTransaction { transaction_id }, Some(token))
            .await;
        let payload = self.clear_session_on_auth_failure(response)?;
        match payload {
            ResponsePayload::TransactionDeleted(result) => Ok(result),
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    /// Get bounded metadata for a transaction receipt. The operation does not
    /// return a filesystem path or an unbounded attachment.
    pub async fn get_transaction_receipt(
        &mut self,
        transaction_id: i64,
    ) -> Result<TransactionReceiptData, ClientError> {
        let token = self.current_token()?;
        let response = self
            .round_trip(
                Command::GetTransactionReceipt { transaction_id },
                Some(token),
            )
            .await;
        let payload = self.clear_session_on_auth_failure(response)?;
        match payload {
            ResponsePayload::TransactionReceipt(receipt) => Ok(receipt),
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    /// List budgets from the authenticated session's explicitly selected store.
    pub async fn list_budgets(&mut self) -> Result<BudgetPage, ClientError> {
        let token = self.current_token()?;
        let response = self.round_trip(Command::ListBudgets, Some(token)).await;
        let payload = self.clear_session_on_auth_failure(response)?;
        match payload {
            ResponsePayload::Budgets(page) => Ok(page),
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    /// Create a budget allocation. The kernel creates the default threshold rules
    /// in the same storage transaction as the allocation.
    pub async fn create_budget(
        &mut self,
        budget: CreateBudgetRequest,
    ) -> Result<BudgetRecord, ClientError> {
        let token = self.current_token()?;
        let response = self
            .round_trip(Command::CreateBudget { budget }, Some(token))
            .await;
        let payload = self.clear_session_on_auth_failure(response)?;
        match payload {
            ResponsePayload::BudgetCreated(record) => Ok(record),
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    /// Update using optimistic revision matching. Stale forms are rejected.
    pub async fn update_budget(
        &mut self,
        budget: UpdateBudgetRequest,
    ) -> Result<BudgetRecord, ClientError> {
        let token = self.current_token()?;
        let response = self
            .round_trip(Command::UpdateBudget { budget }, Some(token))
            .await;
        let payload = self.clear_session_on_auth_failure(response)?;
        match payload {
            ResponsePayload::BudgetUpdated(record) => Ok(record),
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    /// Delete only the matching owner's current budget revision.
    pub async fn delete_budget(
        &mut self,
        budget: DeleteBudgetRequest,
    ) -> Result<DeleteBudgetResult, ClientError> {
        let token = self.current_token()?;
        let response = self
            .round_trip(Command::DeleteBudget { budget }, Some(token))
            .await;
        let payload = self.clear_session_on_auth_failure(response)?;
        match payload {
            ResponsePayload::BudgetDeleted(result) => Ok(result),
            mut other => {
                zeroize_payload(&mut other);
                self.unusable = true;
                Err(ClientError::UnexpectedResponse)
            }
        }
    }

    fn current_token(&self) -> Result<String, ClientError> {
        self.session_token
            .as_ref()
            .map(|token| token.as_str().to_owned())
            .ok_or(ClientError::NotAuthenticated)
    }

    fn clear_session_on_auth_failure(
        &mut self,
        response: Result<ResponsePayload, ClientError>,
    ) -> Result<ResponsePayload, ClientError> {
        match response {
            Ok(payload) => Ok(payload),
            Err(error) => {
                if matches!(error, ClientError::Remote(ErrorCode::Unauthenticated)) {
                    self.clear_local_session();
                }
                Err(error)
            }
        }
    }

    fn allocate_request_id(&mut self) -> Result<u64, ClientError> {
        let request_id = self
            .next_request_id
            .ok_or(ClientError::RequestIdExhausted)?;
        self.next_request_id = request_id.checked_add(1);
        Ok(request_id)
    }

    async fn round_trip(
        &mut self,
        command: Command,
        session_token: Option<String>,
    ) -> Result<ResponsePayload, ClientError> {
        let mut request = RequestEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: 0,
            session_token,
            command,
        };

        if self.unusable {
            zeroize_request(&mut request);
            return Err(ClientError::ConnectionUnusable);
        }

        let request_id = match self.allocate_request_id() {
            Ok(request_id) => request_id,
            Err(error) => {
                zeroize_request(&mut request);
                return Err(error);
            }
        };
        request.request_id = request_id;

        if request.validate().is_err() {
            zeroize_request(&mut request);
            return Err(ClientError::InvalidRequest);
        }

        let encoded_request = encode_payload(&request);
        zeroize_request(&mut request);
        let payload = match encoded_request {
            Ok(payload) => {
                let payload = Zeroizing::new(payload);
                if payload.len() > MAX_PAYLOAD_BYTES {
                    return Err(ClientError::PayloadTooLarge);
                }
                payload
            }
            Err(fintally_ipc_protocol::CodecError::PayloadTooLarge) => {
                return Err(ClientError::PayloadTooLarge)
            }
            Err(_) => return Err(ClientError::EncodeFailed),
        };

        let exchange = with_timeout(
            self.config.request_timeout(),
            self.transport.exchange_payload(payload.as_slice()),
        )
        .await;

        let response_bytes = match exchange {
            Err(_) => {
                self.unusable = true;
                return Err(ClientError::RequestTimeout);
            }
            Ok(Err(error)) => {
                self.unusable = true;
                return Err(map_transport_error(error));
            }
            Ok(Ok(bytes)) => Zeroizing::new(bytes),
        };

        if response_bytes.len() > MAX_PAYLOAD_BYTES {
            self.unusable = true;
            return Err(ClientError::PayloadTooLarge);
        }

        let mut response: ResponseEnvelope = match decode_payload(response_bytes.as_slice()) {
            Ok(response) => response,
            Err(_) => {
                self.unusable = true;
                return Err(ClientError::MalformedResponse);
            }
        };

        if response.protocol_version != IPC_PROTOCOL_VERSION {
            self.unusable = true;
            zeroize_response(&mut response);
            return Err(ClientError::IncompatibleProtocolVersion);
        }
        if response.request_id != request_id {
            self.unusable = true;
            zeroize_response(&mut response);
            return Err(ClientError::RequestIdMismatch);
        }

        match response.result {
            Ok(payload) => Ok(payload),
            Err(error) => Err(ClientError::Remote(error.code)),
        }
    }
}

fn map_transport_error(error: TransportError) -> ClientError {
    match error {
        TransportError::InvalidFrameHeader
        | TransportError::PayloadTooLarge
        | TransportError::InvalidFrameLength => ClientError::InvalidFrame,
        TransportError::AllocationFailed => ClientError::Transport,
        TransportError::Io | TransportError::Closed => ClientError::Transport,
    }
}

fn zeroize_request(request: &mut RequestEnvelope) {
    match &mut request.command {
        Command::Login { email, password }
        | Command::SignUp {
            email, password, ..
        }
        | Command::LoginWithStorage {
            email, password, ..
        } => {
            email.zeroize();
            password.zeroize();
        }
        Command::GoogleSignIn { id_token, .. } | Command::GoogleSignUp { id_token, .. } => {
            id_token.zeroize()
        }
        Command::CreateTransaction { transaction } => {
            transaction.name.zeroize();
            transaction.price.zeroize();
            transaction.description.zeroize();
            transaction.datetime_unix_seconds.zeroize();
            transaction.category.zeroize();
            transaction.is_recurring = false;
            transaction.recurring_frequency = None;
        }
        Command::UpdateTransaction { transaction } => {
            zeroize_change_string(&mut transaction.name);
            zeroize_change_f64(&mut transaction.price);
            zeroize_change_string(&mut transaction.description);
            zeroize_change_i64(&mut transaction.datetime_unix_seconds);
            zeroize_change_string(&mut transaction.category);
            zeroize_change_bool(&mut transaction.is_recurring);
            transaction.recurring_frequency = FieldChange::Unchanged;
            transaction.id.zeroize();
        }
        Command::DeleteTransaction { transaction_id }
        | Command::GetTransactionReceipt { transaction_id } => transaction_id.zeroize(),
        Command::CreateBudget { budget } => {
            budget.category.zeroize();
            budget.amount_minor_units.zeroize();
            budget.period_start_unix.zeroize();
            if let Some(period_end) = budget.period_end_unix.as_mut() {
                period_end.zeroize();
            }
        }
        Command::UpdateBudget { budget } => {
            budget.id.zeroize();
            budget.expected_revision.zeroize();
            budget.category.zeroize();
            budget.amount_minor_units.zeroize();
            budget.period_start_unix.zeroize();
            if let Some(period_end) = budget.period_end_unix.as_mut() {
                period_end.zeroize();
            }
        }
        Command::DeleteBudget { budget } => {
            budget.id.zeroize();
            budget.expected_revision.zeroize();
        }
        Command::Logout
        | Command::CheckSession
        | Command::ListTransactions { .. }
        | Command::ListBudgets => {}
    }
    if let Some(token) = request.session_token.as_mut() {
        token.zeroize();
    }
}

fn zeroize_change_string(change: &mut FieldChange<String>) {
    if let FieldChange::Set(value) = change {
        value.zeroize();
    }
    *change = FieldChange::Unchanged;
}

fn zeroize_change_f64(change: &mut FieldChange<f64>) {
    if let FieldChange::Set(value) = change {
        value.zeroize();
    }
    *change = FieldChange::Unchanged;
}

fn zeroize_change_i64(change: &mut FieldChange<i64>) {
    if let FieldChange::Set(value) = change {
        value.zeroize();
    }
    *change = FieldChange::Unchanged;
}

fn zeroize_change_bool(change: &mut FieldChange<bool>) {
    if let FieldChange::Set(value) = change {
        *value = false;
    }
    *change = FieldChange::Unchanged;
}

fn zeroize_record(record: &mut TransactionRecord) {
    record.id.zeroize();
    record.name.zeroize();
    record.price.zeroize();
    record.description.zeroize();
    record.datetime_unix_seconds.zeroize();
    record.category.zeroize();
    record.is_recurring.zeroize();
    record.recurring_frequency = None;
    if let Some(receipt_url) = record.receipt_url.as_mut() {
        receipt_url.zeroize();
    }
}

fn zeroize_budget_record(record: &mut BudgetRecord) {
    record.id.zeroize();
    record.category.zeroize();
    record.amount_minor_units.zeroize();
    record.spent_minor_units.zeroize();
    record.remaining_minor_units.zeroize();
    record.utilization_percent_x100.zeroize();
    record.period_start_unix.zeroize();
    if let Some(period_end) = record.period_end_unix.as_mut() {
        period_end.zeroize();
    }
    record.is_active = false;
    record.revision.zeroize();
}

fn zeroize_payload(payload: &mut ResponsePayload) {
    match payload {
        ResponsePayload::Login(login) => login.session_token.zeroize(),
        ResponsePayload::Authenticated(session) => session.session_token.zeroize(),
        ResponsePayload::SessionStatus(_) | ResponsePayload::LoggedOut(_) => {}
        ResponsePayload::Transactions(page) => {
            page.page.zeroize();
            page.page_size.zeroize();
            for record in &mut page.items {
                zeroize_record(record);
            }
            page.items.clear();
        }
        ResponsePayload::TransactionCreated(record)
        | ResponsePayload::TransactionUpdated(record) => {
            zeroize_record(record);
        }
        ResponsePayload::TransactionDeleted(result) => {
            result.transaction_id.zeroize();
            result.deleted = false;
        }
        ResponsePayload::TransactionReceipt(receipt) => {
            receipt.transaction_id.zeroize();
            receipt.name.zeroize();
            receipt.price.zeroize();
            receipt.description.zeroize();
            receipt.datetime_unix_seconds.zeroize();
            receipt.category.zeroize();
        }
        ResponsePayload::Budgets(page) => {
            for record in &mut page.items {
                zeroize_budget_record(record);
            }
            page.items.clear();
        }
        ResponsePayload::BudgetCreated(record) | ResponsePayload::BudgetUpdated(record) => {
            zeroize_budget_record(record)
        }
        ResponsePayload::BudgetDeleted(result) => {
            result.budget_id.zeroize();
            result.deleted = false;
        }
    }
}

fn zeroize_response(response: &mut ResponseEnvelope) {
    if let Ok(payload) = &mut response.result {
        zeroize_payload(payload);
    }
}

/// Auth requests include password hashing and/or remote Google verification;
/// this timeout is intentionally a seconds-scale budget, not a hot-path target.
pub const DEFAULT_REQUEST_TIMEOUT: Duration = Duration::from_secs(5);
