//! Protocol-to-application adapter. The kernel is the authority for identity,
//! session scope and data residency; clients never submit an authoritative user ID.

use crate::handlers::auth::{
    handle_legacy_login, handle_login_with_storage, handle_signup, reject_unverified_google_token,
};
use crate::sessions::SessionIdentity;
use crate::state::KernelState;
use fintally_app::budgets::{self as budgets, BudgetError, BudgetView, NewBudget};
use fintally_app::transactions::{
    self as tx, FieldChange as AppFieldChange, NewTransaction, TransactionError,
    TransactionRecurringFrequency, TransactionUpdate,
};
use fintally_ipc_protocol::{
    BudgetPage, BudgetRecord, Command, CreateBudgetRequest, CreateTransactionRequest,
    DeleteBudgetRequest, DeleteBudgetResult, DeleteTransactionResult, ErrorCode, FieldChange,
    IpcError, LogoutResult, RecurringFrequency, RequestEnvelope, RequestValidationError,
    ResponseEnvelope, ResponsePayload, SessionStatus, StorageMode, TransactionPage,
    TransactionReceiptData, TransactionRecord, UpdateBudgetRequest, UpdateTransactionRequest,
    IPC_PROTOCOL_VERSION,
};
use std::sync::{atomic::Ordering, Arc};
use zeroize::Zeroize;

pub async fn dispatch(state: &Arc<KernelState>, request: RequestEnvelope) -> ResponseEnvelope {
    let request_id = request.request_id;
    let result = match request.validate() {
        Err(error) => Err(IpcError {
            code: validation_error_code(error),
        }),
        Ok(()) => match request.command {
            Command::Login {
                email,
                mut password,
            } => {
                let result = handle_legacy_login(state, &email, &password).await;
                password.zeroize();
                result
                    .map(ResponsePayload::Login)
                    .map_err(|code| IpcError { code })
            }
            Command::SignUp {
                email,
                mut password,
                storage_mode,
            } => {
                let result = handle_signup(state, &email, &password, storage_mode).await;
                password.zeroize();
                result
                    .map(ResponsePayload::Authenticated)
                    .map_err(|code| IpcError { code })
            }
            Command::LoginWithStorage {
                email,
                mut password,
                storage_mode,
            } => {
                let result =
                    handle_login_with_storage(state, &email, &password, storage_mode).await;
                password.zeroize();
                result
                    .map(ResponsePayload::Authenticated)
                    .map_err(|code| IpcError { code })
            }
            Command::GoogleSignIn { id_token, .. } | Command::GoogleSignUp { id_token, .. } => {
                Err(IpcError {
                    code: reject_unverified_google_token(id_token),
                })
            }
            Command::Logout => {
                let token = request.session_token.as_deref().unwrap_or_default();
                let revoked = state.sessions.revoke(token).await;
                Ok(ResponsePayload::LoggedOut(LogoutResult { revoked }))
            }
            Command::CheckSession => {
                let token = request.session_token.as_deref().unwrap_or_default();
                match state.sessions.authenticate_scoped(token).await {
                    Ok(identity) => Ok(ResponsePayload::SessionStatus(SessionStatus {
                        authenticated: true,
                        storage_mode: Some(identity.storage_mode),
                        provider: Some(identity.provider),
                        expires_at_unix_seconds: Some(identity.expires_at_unix_seconds),
                    })),
                    Err(_) => Err(IpcError {
                        code: ErrorCode::Unauthenticated,
                    }),
                }
            }
            Command::ListTransactions { page, page_size } => {
                dispatch_transaction(
                    state,
                    request.session_token.as_deref(),
                    TransactionOperation::List { page, page_size },
                )
                .await
            }
            Command::CreateTransaction { transaction } => {
                dispatch_transaction(
                    state,
                    request.session_token.as_deref(),
                    TransactionOperation::Create(map_create_request(transaction)),
                )
                .await
            }
            Command::UpdateTransaction { transaction } => {
                dispatch_transaction(
                    state,
                    request.session_token.as_deref(),
                    TransactionOperation::Update(map_update_request(transaction)),
                )
                .await
            }
            Command::DeleteTransaction { transaction_id } => {
                dispatch_transaction(
                    state,
                    request.session_token.as_deref(),
                    TransactionOperation::Delete { transaction_id },
                )
                .await
            }
            Command::GetTransactionReceipt { transaction_id } => {
                dispatch_transaction(
                    state,
                    request.session_token.as_deref(),
                    TransactionOperation::Receipt { transaction_id },
                )
                .await
            }
            Command::ListBudgets => {
                dispatch_budget(
                    state,
                    request.session_token.as_deref(),
                    BudgetOperation::List,
                )
                .await
            }
            Command::CreateBudget { budget } => {
                dispatch_budget(
                    state,
                    request.session_token.as_deref(),
                    BudgetOperation::Create(map_new_budget(budget)),
                )
                .await
            }
            Command::UpdateBudget { budget } => {
                dispatch_budget(
                    state,
                    request.session_token.as_deref(),
                    BudgetOperation::Update {
                        id: budget.id,
                        expected_revision: budget.expected_revision,
                        input: map_new_budget_from_update(budget),
                    },
                )
                .await
            }
            Command::DeleteBudget { budget } => {
                dispatch_budget(
                    state,
                    request.session_token.as_deref(),
                    BudgetOperation::Delete {
                        id: budget.id,
                        expected_revision: budget.expected_revision,
                    },
                )
                .await
            }
        },
    };

    if result.is_err() {
        state.metrics.dispatch_error();
    }

    ResponseEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id,
        result,
    }
}

enum BudgetOperation {
    List,
    Create(NewBudget),
    Update {
        id: i64,
        expected_revision: i64,
        input: NewBudget,
    },
    Delete {
        id: i64,
        expected_revision: i64,
    },
}

async fn dispatch_budget(
    state: &Arc<KernelState>,
    session_token: Option<&str>,
    operation: BudgetOperation,
) -> Result<ResponsePayload, IpcError> {
    let identity = state
        .sessions
        .authenticate_scoped(session_token.unwrap_or_default())
        .await
        .map_err(|_| IpcError {
            code: ErrorCode::Unauthenticated,
        })?;

    // The CloudSync worker applies the complete additive migration directory.
    // Never route a budget request to a cloud schema before that migration is ready.
    if identity.storage_mode == StorageMode::CloudSync
        && !state.cloud_recurring_schema_ready.load(Ordering::Acquire)
    {
        return Err(IpcError {
            code: ErrorCode::Busy,
        });
    }

    perform_budget(state, &identity, operation)
        .await
        .map_err(|error| {
            let code = budget_error_code(&error);
            tracing::warn!(error = %error, "budget operation failed");
            IpcError { code }
        })
}

async fn perform_budget(
    state: &Arc<KernelState>,
    identity: &SessionIdentity,
    operation: BudgetOperation,
) -> Result<ResponsePayload, BudgetError> {
    let now_unix = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .ok()
        .and_then(|duration| i64::try_from(duration.as_secs()).ok())
        .ok_or(BudgetError::InvalidInput)?;

    match (identity.storage_mode, operation) {
        (StorageMode::LocalOnly, BudgetOperation::List) => {
            let items = budgets::list_budgets_local(&state.local_db, &identity.principal).await?;
            Ok(ResponsePayload::Budgets(map_budget_page(items)))
        }
        (StorageMode::CloudSync, BudgetOperation::List) => {
            let db = state.db.as_ref().ok_or(BudgetError::Database)?;
            let items = budgets::list_budgets(db, &identity.principal).await?;
            Ok(ResponsePayload::Budgets(map_budget_page(items)))
        }
        (StorageMode::LocalOnly, BudgetOperation::Create(input)) => {
            let record =
                budgets::create_budget_local(&state.local_db, &identity.principal, input, now_unix)
                    .await?;
            Ok(ResponsePayload::BudgetCreated(map_budget_record(record)))
        }
        (StorageMode::CloudSync, BudgetOperation::Create(input)) => {
            let db = state.db.as_ref().ok_or(BudgetError::Database)?;
            let record = budgets::create_budget(db, &identity.principal, input, now_unix).await?;
            Ok(ResponsePayload::BudgetCreated(map_budget_record(record)))
        }
        (
            StorageMode::LocalOnly,
            BudgetOperation::Update {
                id,
                expected_revision,
                input,
            },
        ) => {
            let record = budgets::update_budget_local(
                &state.local_db,
                &identity.principal,
                id,
                expected_revision,
                input,
                now_unix,
            )
            .await?;
            Ok(ResponsePayload::BudgetUpdated(map_budget_record(record)))
        }
        (
            StorageMode::CloudSync,
            BudgetOperation::Update {
                id,
                expected_revision,
                input,
            },
        ) => {
            let db = state.db.as_ref().ok_or(BudgetError::Database)?;
            let record = budgets::update_budget(
                db,
                &identity.principal,
                id,
                expected_revision,
                input,
                now_unix,
            )
            .await?;
            Ok(ResponsePayload::BudgetUpdated(map_budget_record(record)))
        }
        (
            StorageMode::LocalOnly,
            BudgetOperation::Delete {
                id,
                expected_revision,
            },
        ) => {
            let result = budgets::delete_budget_local(
                &state.local_db,
                &identity.principal,
                id,
                expected_revision,
            )
            .await?;
            Ok(ResponsePayload::BudgetDeleted(map_budget_delete(result)))
        }
        (
            StorageMode::CloudSync,
            BudgetOperation::Delete {
                id,
                expected_revision,
            },
        ) => {
            let db = state.db.as_ref().ok_or(BudgetError::Database)?;
            let result =
                budgets::delete_budget(db, &identity.principal, id, expected_revision).await?;
            Ok(ResponsePayload::BudgetDeleted(map_budget_delete(result)))
        }
    }
}

fn map_new_budget(input: CreateBudgetRequest) -> NewBudget {
    NewBudget {
        category: input.category,
        amount_minor_units: input.amount_minor_units,
        period_start_unix: input.period_start_unix,
        period_end_unix: input.period_end_unix,
    }
}

fn map_new_budget_from_update(input: UpdateBudgetRequest) -> NewBudget {
    NewBudget {
        category: input.category,
        amount_minor_units: input.amount_minor_units,
        period_start_unix: input.period_start_unix,
        period_end_unix: input.period_end_unix,
    }
}

fn map_budget_page(items: Vec<BudgetView>) -> BudgetPage {
    BudgetPage {
        items: items.into_iter().map(map_budget_record).collect(),
    }
}

fn map_budget_record(record: BudgetView) -> BudgetRecord {
    BudgetRecord {
        id: record.id,
        category: record.category,
        amount_minor_units: record.amount_minor_units,
        spent_minor_units: record.spent_minor_units,
        remaining_minor_units: record.remaining_minor_units,
        utilization_percent_x100: record.utilization_percent_x100,
        period_start_unix: record.period_start_unix,
        period_end_unix: record.period_end_unix,
        is_active: record.is_active,
        revision: record.revision,
    }
}

fn map_budget_delete(result: budgets::BudgetDeleteResult) -> DeleteBudgetResult {
    DeleteBudgetResult {
        budget_id: result.budget_id,
        deleted: result.deleted,
    }
}

fn budget_error_code(error: &BudgetError) -> ErrorCode {
    match error {
        BudgetError::InvalidInput | BudgetError::ResponseTooLarge => ErrorCode::InvalidRequest,
        BudgetError::NotFoundOrStale | BudgetError::PeriodConflict => ErrorCode::Conflict,
        BudgetError::Database | BudgetError::InvalidStoredRecord => ErrorCode::Internal,
    }
}

enum TransactionOperation {
    List { page: u32, page_size: u16 },
    Create(NewTransaction),
    Update(TransactionUpdate),
    Delete { transaction_id: i64 },
    Receipt { transaction_id: i64 },
}

async fn dispatch_transaction(
    state: &Arc<KernelState>,
    session_token: Option<&str>,
    operation: TransactionOperation,
) -> Result<ResponsePayload, IpcError> {
    let token = session_token.unwrap_or_default();
    let identity = state
        .sessions
        .authenticate_scoped(token)
        .await
        .map_err(|_| IpcError {
            code: ErrorCode::Unauthenticated,
        })?;

    // CloudSync UPDATE now resets recurrence metadata in the same SQL
    // statement. Do not race that query against the additive cloud migration.
    if identity.storage_mode == StorageMode::CloudSync
        && matches!(&operation, TransactionOperation::Update(_))
        && !state.cloud_recurring_schema_ready.load(Ordering::Acquire)
    {
        return Err(IpcError {
            code: ErrorCode::Busy,
        });
    }

    perform_transaction(state, &identity, operation)
        .await
        .map_err(|error| {
            let code = transaction_error_code(&error);
            tracing::warn!(error = %error, "transaction operation failed");
            IpcError { code }
        })
}

async fn perform_transaction(
    state: &Arc<KernelState>,
    identity: &SessionIdentity,
    operation: TransactionOperation,
) -> Result<ResponsePayload, TransactionError> {
    match identity.storage_mode {
        StorageMode::LocalOnly => match operation {
            TransactionOperation::List { page, page_size } => {
                tx::list_transactions_local(&state.local_db, &identity.principal, page, page_size)
                    .await
                    .map(map_transaction_page)
                    .map(ResponsePayload::Transactions)
            }
            TransactionOperation::Create(input) => {
                tx::create_transaction_local(&state.local_db, &identity.principal, input)
                    .await
                    .map(map_transaction_record)
                    .map(ResponsePayload::TransactionCreated)
            }
            TransactionOperation::Update(input) => {
                tx::update_transaction_local(&state.local_db, &identity.principal, input)
                    .await
                    .map(map_transaction_record)
                    .map(ResponsePayload::TransactionUpdated)
            }
            TransactionOperation::Delete { transaction_id } => {
                tx::delete_transaction_local(&state.local_db, &identity.principal, transaction_id)
                    .await
                    .map(map_delete_result)
                    .map(ResponsePayload::TransactionDeleted)
            }
            TransactionOperation::Receipt { transaction_id } => tx::get_transaction_receipt_local(
                &state.local_db,
                &identity.principal,
                transaction_id,
            )
            .await
            .map(map_receipt_data)
            .map(ResponsePayload::TransactionReceipt),
        },
        StorageMode::CloudSync => {
            let db = state.db.as_ref().ok_or(TransactionError::Database)?;
            match operation {
                TransactionOperation::List { page, page_size } => {
                    tx::list_transactions(db, &identity.principal, page, page_size)
                        .await
                        .map(map_transaction_page)
                        .map(ResponsePayload::Transactions)
                }
                TransactionOperation::Create(input) => {
                    tx::create_transaction(db, &identity.principal, input)
                        .await
                        .map(map_transaction_record)
                        .map(ResponsePayload::TransactionCreated)
                }
                TransactionOperation::Update(input) => {
                    tx::update_transaction(db, &identity.principal, input)
                        .await
                        .map(map_transaction_record)
                        .map(ResponsePayload::TransactionUpdated)
                }
                TransactionOperation::Delete { transaction_id } => {
                    tx::delete_transaction(db, &identity.principal, transaction_id)
                        .await
                        .map(map_delete_result)
                        .map(ResponsePayload::TransactionDeleted)
                }
                TransactionOperation::Receipt { transaction_id } => {
                    tx::get_transaction_receipt(db, &identity.principal, transaction_id)
                        .await
                        .map(map_receipt_data)
                        .map(ResponsePayload::TransactionReceipt)
                }
            }
        }
    }
}

fn map_create_request(request: CreateTransactionRequest) -> NewTransaction {
    NewTransaction {
        name: request.name,
        price: request.price,
        description: request.description,
        datetime_unix_seconds: request.datetime_unix_seconds,
        category: request.category,
        is_recurring: request.is_recurring,
        recurring_frequency: request.recurring_frequency.map(map_recurring_frequency),
    }
}

fn map_update_request(request: UpdateTransactionRequest) -> TransactionUpdate {
    TransactionUpdate {
        id: request.id,
        name: map_field_change(request.name, |value| value),
        price: map_field_change(request.price, |value| value),
        description: map_field_change(request.description, |value| value),
        datetime_unix_seconds: map_field_change(request.datetime_unix_seconds, |value| value),
        category: map_field_change(request.category, |value| value),
        is_recurring: map_field_change(request.is_recurring, |value| value),
        recurring_frequency: map_field_change(request.recurring_frequency, map_recurring_frequency),
    }
}

fn map_field_change<T, U>(change: FieldChange<T>, map: impl FnOnce(T) -> U) -> AppFieldChange<U> {
    match change {
        FieldChange::Unchanged => AppFieldChange::Unchanged,
        FieldChange::Set(value) => AppFieldChange::Set(map(value)),
        FieldChange::Clear => AppFieldChange::Clear,
    }
}

fn map_recurring_frequency(frequency: RecurringFrequency) -> TransactionRecurringFrequency {
    match frequency {
        RecurringFrequency::Daily => TransactionRecurringFrequency::Daily,
        RecurringFrequency::Weekly => TransactionRecurringFrequency::Weekly,
        RecurringFrequency::Monthly => TransactionRecurringFrequency::Monthly,
    }
}

fn map_transaction_page(page: tx::TransactionPage) -> TransactionPage {
    TransactionPage {
        page: page.page,
        page_size: page.page_size,
        has_more: page.has_more,
        items: page.items.into_iter().map(map_transaction_record).collect(),
    }
}

fn map_transaction_record(record: tx::TransactionRecord) -> TransactionRecord {
    TransactionRecord {
        id: record.id,
        name: record.name,
        price: record.price,
        description: record.description,
        datetime_unix_seconds: record.datetime_unix_seconds,
        category: record.category,
        is_recurring: record.is_recurring,
        recurring_frequency: record.recurring_frequency.map(|frequency| match frequency {
            TransactionRecurringFrequency::Daily => RecurringFrequency::Daily,
            TransactionRecurringFrequency::Weekly => RecurringFrequency::Weekly,
            TransactionRecurringFrequency::Monthly => RecurringFrequency::Monthly,
        }),
        receipt_url: record.receipt_url,
    }
}

fn map_delete_result(result: tx::TransactionDeleteResult) -> DeleteTransactionResult {
    DeleteTransactionResult {
        transaction_id: result.transaction_id,
        deleted: result.deleted,
    }
}

fn map_receipt_data(receipt: tx::TransactionReceiptData) -> TransactionReceiptData {
    TransactionReceiptData {
        transaction_id: receipt.transaction_id,
        name: receipt.name,
        price: receipt.price,
        description: receipt.description,
        datetime_unix_seconds: receipt.datetime_unix_seconds,
        category: receipt.category,
    }
}

fn validation_error_code(error: RequestValidationError) -> ErrorCode {
    match error {
        RequestValidationError::UnsupportedProtocolVersion => ErrorCode::UnsupportedProtocolVersion,
        RequestValidationError::MissingSessionToken
        | RequestValidationError::InvalidSessionToken => ErrorCode::Unauthenticated,
        RequestValidationError::InvalidPagination
        | RequestValidationError::InvalidRequestId
        | RequestValidationError::UnexpectedSessionToken
        | RequestValidationError::InvalidCredentialsPayload
        | RequestValidationError::InvalidGoogleCredential
        | RequestValidationError::InvalidTransactionPayload
        | RequestValidationError::InvalidBudgetPayload => ErrorCode::InvalidRequest,
    }
}

fn transaction_error_code(error: &TransactionError) -> ErrorCode {
    match error {
        TransactionError::InvalidInput
        | TransactionError::InvalidPagination
        | TransactionError::ResponseTooLarge => ErrorCode::InvalidRequest,
        TransactionError::NotFound => ErrorCode::NotFound,
        TransactionError::Database
        | TransactionError::InvalidStoredPrice
        | TransactionError::InvalidStoredFieldLength
        | TransactionError::InvalidStoredRecord
        | TransactionError::InvalidStoredRecurringFrequency => ErrorCode::Internal,
    }
}

#[cfg(test)]
mod tests {
    use super::{transaction_error_code, validation_error_code};
    use fintally_app::transactions::TransactionError;
    use fintally_ipc_protocol::{ErrorCode, RequestValidationError};

    #[test]
    fn maps_missing_token_to_unauthenticated() {
        assert_eq!(
            validation_error_code(RequestValidationError::MissingSessionToken),
            ErrorCode::Unauthenticated
        );
    }

    #[test]
    fn maps_invalid_google_credential_to_invalid_request() {
        assert_eq!(
            validation_error_code(RequestValidationError::InvalidGoogleCredential),
            ErrorCode::InvalidRequest
        );
    }

    #[test]
    fn maps_invalid_transaction_payload_to_invalid_request() {
        assert_eq!(
            validation_error_code(RequestValidationError::InvalidTransactionPayload),
            ErrorCode::InvalidRequest
        );
    }

    #[test]
    fn maps_not_found_without_database_diagnostics() {
        assert_eq!(
            transaction_error_code(&TransactionError::NotFound),
            ErrorCode::NotFound
        );
        assert_eq!(
            transaction_error_code(&TransactionError::Database),
            ErrorCode::Internal
        );
    }

    #[test]
    fn maps_oversized_response_to_invalid_request() {
        assert_eq!(
            transaction_error_code(&TransactionError::ResponseTooLarge),
            ErrorCode::InvalidRequest
        );
    }
}
