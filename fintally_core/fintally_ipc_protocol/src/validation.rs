//! Cheap deterministic validation for untrusted IPC requests.
//!
//! Validation never authenticates tokens, verifies Google signatures, or opens
//! storage. Those decisions belong to the kernel/application layer.

use crate::auth_limits::{
    MAX_AUTH_PASSWORD_BYTES, MAX_GOOGLE_ID_TOKEN_BYTES, MIN_SIGNUP_PASSWORD_BYTES,
};
use crate::command::Command;
use crate::dto::{CreateTransactionRequest, FieldChange, UpdateTransactionRequest};
use crate::envelope::RequestEnvelope;
use crate::limits::{MAX_EMAIL_BYTES, MAX_PAGE, MAX_PAGE_SIZE, MAX_SESSION_TOKEN_BYTES};
use crate::version::is_supported_protocol_version;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RequestValidationError {
    UnsupportedProtocolVersion,
    InvalidRequestId,
    UnexpectedSessionToken,
    InvalidCredentialsPayload,
    InvalidGoogleCredential,
    MissingSessionToken,
    InvalidSessionToken,
    InvalidPagination,
    InvalidTransactionPayload,
    InvalidBudgetPayload,
}

impl RequestEnvelope {
    pub fn validate(&self) -> Result<(), RequestValidationError> {
        if !is_supported_protocol_version(self.protocol_version) {
            return Err(RequestValidationError::UnsupportedProtocolVersion);
        }
        if self.request_id == 0 {
            return Err(RequestValidationError::InvalidRequestId);
        }

        match &self.command {
            Command::Login { email, password } => {
                validate_unauthenticated_envelope(self)?;
                validate_email_password(email, password, false)?;
            }
            Command::SignUp {
                email, password, ..
            } => {
                validate_unauthenticated_envelope(self)?;
                validate_email_password(email, password, true)?;
            }
            Command::LoginWithStorage {
                email, password, ..
            } => {
                validate_unauthenticated_envelope(self)?;
                validate_email_password(email, password, false)?;
            }
            Command::GoogleSignIn { id_token, .. } | Command::GoogleSignUp { id_token, .. } => {
                validate_unauthenticated_envelope(self)?;
                if !is_compact_jwt(id_token) || id_token.len() > MAX_GOOGLE_ID_TOKEN_BYTES {
                    return Err(RequestValidationError::InvalidGoogleCredential);
                }
            }
            Command::Logout
            | Command::CheckSession
            | Command::ListTransactions { .. }
            | Command::CreateTransaction { .. }
            | Command::UpdateTransaction { .. }
            | Command::DeleteTransaction { .. }
            | Command::GetTransactionReceipt { .. }
            | Command::ListBudgets
            | Command::CreateBudget { .. }
            | Command::UpdateBudget { .. }
            | Command::DeleteBudget { .. } => {
                validate_session_token(self.session_token.as_deref())?;
                match &self.command {
                    Command::ListTransactions { page, page_size } => {
                        if *page == 0
                            || *page > MAX_PAGE
                            || *page_size == 0
                            || *page_size > MAX_PAGE_SIZE
                        {
                            return Err(RequestValidationError::InvalidPagination);
                        }
                    }
                    Command::CreateTransaction { transaction } => {
                        if !valid_create_transaction(transaction) {
                            return Err(RequestValidationError::InvalidTransactionPayload);
                        }
                    }
                    Command::UpdateTransaction { transaction } => {
                        if !valid_update_transaction(transaction) {
                            return Err(RequestValidationError::InvalidTransactionPayload);
                        }
                    }
                    Command::DeleteTransaction { transaction_id }
                    | Command::GetTransactionReceipt { transaction_id }
                        if *transaction_id <= 0 =>
                    {
                        return Err(RequestValidationError::InvalidTransactionPayload);
                    }
                    Command::CreateBudget { budget } => {
                        if !valid_budget(
                            &budget.category,
                            budget.amount_minor_units,
                            budget.period_start_unix,
                            budget.period_end_unix,
                        ) {
                            return Err(RequestValidationError::InvalidBudgetPayload);
                        }
                    }
                    Command::UpdateBudget { budget } => {
                        if budget.id <= 0
                            || budget.expected_revision <= 0
                            || !valid_budget(
                                &budget.category,
                                budget.amount_minor_units,
                                budget.period_start_unix,
                                budget.period_end_unix,
                            )
                        {
                            return Err(RequestValidationError::InvalidBudgetPayload);
                        }
                    }
                    Command::DeleteBudget { budget } => {
                        if budget.id <= 0 || budget.expected_revision <= 0 {
                            return Err(RequestValidationError::InvalidBudgetPayload);
                        }
                    }
                    _ => {}
                }
            }
        }
        Ok(())
    }
}

fn valid_budget(
    category: &str,
    amount_minor_units: i64,
    period_start_unix: i64,
    period_end_unix: Option<i64>,
) -> bool {
    !category.trim().is_empty()
        && category.len() <= crate::limits::MAX_BUDGET_CATEGORY_BYTES
        && !category.chars().any(char::is_control)
        && amount_minor_units > 0
        && period_end_unix.is_none_or(|end| end >= period_start_unix)
}

fn valid_create_transaction(value: &CreateTransactionRequest) -> bool {
    !value.name.trim().is_empty()
        && value.name.len() <= crate::limits::MAX_TRANSACTION_NAME_BYTES
        && value.description.len() <= crate::limits::MAX_TRANSACTION_DESCRIPTION_BYTES
        && !value.category.trim().is_empty()
        && value.category.len() <= crate::limits::MAX_TRANSACTION_CATEGORY_BYTES
        && value.price.is_finite()
        && value.is_recurring == value.recurring_frequency.is_some()
}

fn valid_update_transaction(value: &UpdateTransactionRequest) -> bool {
    use FieldChange::{Clear, Set, Unchanged};

    if value.id <= 0 {
        return false;
    }
    let any_change = !matches!(&value.name, Unchanged)
        || !matches!(&value.price, Unchanged)
        || !matches!(&value.description, Unchanged)
        || !matches!(&value.datetime_unix_seconds, Unchanged)
        || !matches!(&value.category, Unchanged)
        || !matches!(&value.is_recurring, Unchanged)
        || !matches!(&value.recurring_frequency, Unchanged);
    if !any_change {
        return false;
    }

    let valid_name = match &value.name {
        Unchanged => true,
        Set(name) => {
            !name.trim().is_empty() && name.len() <= crate::limits::MAX_TRANSACTION_NAME_BYTES
        }
        Clear => false,
    };
    let valid_price = match &value.price {
        Unchanged => true,
        Set(price) => price.is_finite(),
        Clear => false,
    };
    let valid_datetime = match &value.datetime_unix_seconds {
        Unchanged | Set(_) => true,
        Clear => false,
    };
    let valid_description = match &value.description {
        Unchanged => true,
        Set(description) => description.len() <= crate::limits::MAX_TRANSACTION_DESCRIPTION_BYTES,
        Clear => false,
    };
    let valid_category = match &value.category {
        Unchanged => true,
        Set(category) => {
            !category.trim().is_empty()
                && category.len() <= crate::limits::MAX_TRANSACTION_CATEGORY_BYTES
        }
        Clear => false,
    };
    let valid_recurring = match (&value.is_recurring, &value.recurring_frequency) {
        // A newly enabled or reconfigured schedule must explicitly carry its
        // anchor timestamp; otherwise the kernel could activate a schedule
        // whose persisted anchor has not been validated by this request.
        (Set(true), Set(_)) => matches!(&value.datetime_unix_seconds, Set(_)),
        // Turning recurrence off without supplying a frequency is normalized
        // by the application service to an explicit Clear before persistence.
        (Set(false), Clear | Unchanged) => true,
        (Unchanged, Unchanged) => true,
        (Unchanged, Set(_))
        | (Set(true), Unchanged)
        | (Set(false), Set(_))
        | (Unchanged, Clear)
        | (Set(true), Clear) => false,
        _ => false,
    };

    valid_name
        && valid_price
        && valid_datetime
        && valid_description
        && valid_category
        && valid_recurring
}

fn validate_unauthenticated_envelope(
    request: &RequestEnvelope,
) -> Result<(), RequestValidationError> {
    if request.session_token.is_some() {
        Err(RequestValidationError::UnexpectedSessionToken)
    } else {
        Ok(())
    }
}

fn validate_email_password(
    email: &str,
    password: &str,
    signup: bool,
) -> Result<(), RequestValidationError> {
    let bad_email = email.trim().is_empty()
        || email.len() > MAX_EMAIL_BYTES
        || email.trim() != email
        || email.chars().any(char::is_whitespace);
    let bad_password = password.is_empty()
        || password.len() > MAX_AUTH_PASSWORD_BYTES
        || (signup && password.len() < MIN_SIGNUP_PASSWORD_BYTES);

    if bad_email || bad_password {
        return Err(RequestValidationError::InvalidCredentialsPayload);
    }
    Ok(())
}

fn validate_session_token(token: Option<&str>) -> Result<(), RequestValidationError> {
    let token = token.ok_or(RequestValidationError::MissingSessionToken)?;
    if token.trim().is_empty() || token.len() > MAX_SESSION_TOKEN_BYTES {
        return Err(RequestValidationError::InvalidSessionToken);
    }
    Ok(())
}

fn is_compact_jwt(token: &str) -> bool {
    let mut segments = token.split('.');
    matches!(
        (segments.next(), segments.next(), segments.next(), segments.next()),
        (Some(header), Some(payload), Some(signature), None)
            if !header.is_empty()
                && !payload.is_empty()
                && !signature.is_empty()
                && !token.bytes().any(|byte| byte.is_ascii_whitespace())
    )
}

#[cfg(test)]
mod tests {
    use super::RequestValidationError as Error;
    use crate::auth_limits::MAX_AUTH_PASSWORD_BYTES;
    use crate::command::Command;
    use crate::dto::StorageMode;
    use crate::envelope::RequestEnvelope;
    use crate::limits::{
        DEFAULT_PAGE_SIZE, MAX_EMAIL_BYTES, MAX_PAGE_SIZE, MAX_SESSION_TOKEN_BYTES,
    };
    use crate::version::IPC_PROTOCOL_VERSION;

    fn request(command: Command, session_token: Option<&str>) -> RequestEnvelope {
        RequestEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: 1,
            session_token: session_token.map(str::to_owned),
            command,
        }
    }

    fn legacy_login(email: String, password: String) -> RequestEnvelope {
        request(Command::Login { email, password }, None)
    }

    #[test]
    fn accepts_legacy_login_without_a_session() {
        assert_eq!(
            legacy_login("user@example.com".into(), "short-old-password".into()).validate(),
            Ok(())
        );
    }

    #[test]
    fn accepts_local_signup_at_the_minimum_password_length() {
        let request = request(
            Command::SignUp {
                email: "private@example.com".into(),
                password: "12345678".into(),
                storage_mode: StorageMode::LocalOnly,
            },
            None,
        );
        assert_eq!(request.validate(), Ok(()));
    }

    #[test]
    fn signup_rejects_passwords_below_policy_minimum() {
        let request = request(
            Command::SignUp {
                email: "private@example.com".into(),
                password: "short".into(),
                storage_mode: StorageMode::LocalOnly,
            },
            None,
        );
        assert_eq!(request.validate(), Err(Error::InvalidCredentialsPayload));
    }

    #[test]
    fn local_login_contract_is_explicit() {
        let request = request(
            Command::LoginWithStorage {
                email: "private@example.com".into(),
                password: "a-valid-password".into(),
                storage_mode: StorageMode::LocalOnly,
            },
            None,
        );
        assert_eq!(request.validate(), Ok(()));
    }

    #[test]
    fn google_credential_requires_three_nonempty_compact_jwt_segments() {
        let good = request(
            Command::GoogleSignIn {
                id_token: "header.payload.signature".into(),
                storage_mode: StorageMode::LocalOnly,
            },
            None,
        );
        assert_eq!(good.validate(), Ok(()));
        let signup = request(
            Command::GoogleSignUp {
                id_token: "header.payload.signature".into(),
                storage_mode: StorageMode::LocalOnly,
            },
            None,
        );
        assert_eq!(signup.validate(), Ok(()));
        let bad = request(
            Command::GoogleSignIn {
                id_token: "not-a-token".into(),
                storage_mode: StorageMode::CloudSync,
            },
            None,
        );
        assert_eq!(bad.validate(), Err(Error::InvalidGoogleCredential));
    }

    #[test]
    fn rejects_zero_request_id_and_unsupported_version() {
        let mut value = legacy_login("a@b.co".into(), "password".into());
        value.request_id = 0;
        assert_eq!(value.validate(), Err(Error::InvalidRequestId));

        let mut value = legacy_login("a@b.co".into(), "password".into());
        value.protocol_version = IPC_PROTOCOL_VERSION.saturating_add(1);
        assert_eq!(value.validate(), Err(Error::UnsupportedProtocolVersion));
    }

    #[test]
    fn rejects_blank_or_oversized_credentials() {
        assert_eq!(
            legacy_login("  ".into(), "password".into()).validate(),
            Err(Error::InvalidCredentialsPayload)
        );
        assert_eq!(
            legacy_login("a".repeat(MAX_EMAIL_BYTES + 1), "password".into()).validate(),
            Err(Error::InvalidCredentialsPayload)
        );
        assert_eq!(
            legacy_login("a@b.co".into(), "p".repeat(MAX_AUTH_PASSWORD_BYTES + 1)).validate(),
            Err(Error::InvalidCredentialsPayload)
        );
    }

    #[test]
    fn rejects_tokens_on_authentication_requests() {
        let value = request(
            Command::LoginWithStorage {
                email: "user@example.com".into(),
                password: "correct-horse-battery".into(),
                storage_mode: StorageMode::CloudSync,
            },
            Some("unexpected"),
        );
        assert_eq!(value.validate(), Err(Error::UnexpectedSessionToken));
    }

    #[test]
    fn protected_commands_require_valid_token() {
        assert_eq!(
            request(Command::CheckSession, None).validate(),
            Err(Error::MissingSessionToken)
        );
        assert_eq!(
            request(Command::Logout, Some(" ")).validate(),
            Err(Error::InvalidSessionToken)
        );
        let token = "x".repeat(MAX_SESSION_TOKEN_BYTES + 1);
        assert_eq!(
            request(Command::CheckSession, Some(&token)).validate(),
            Err(Error::InvalidSessionToken)
        );
    }

    #[test]
    fn pagination_remains_bounded() {
        let valid_request = request(
            Command::ListTransactions {
                page: 1,
                page_size: DEFAULT_PAGE_SIZE,
            },
            Some("token"),
        );
        assert_eq!(valid_request.validate(), Ok(()));
        let request = request(
            Command::ListTransactions {
                page: 1,
                page_size: MAX_PAGE_SIZE + 1,
            },
            Some("token"),
        );
        assert_eq!(request.validate(), Err(Error::InvalidPagination));
    }
}

#[cfg(test)]
mod transaction_validation_tests {
    use super::{RequestEnvelope, RequestValidationError};
    use crate::command::Command;
    use crate::dto::{
        CreateTransactionRequest, FieldChange, RecurringFrequency, UpdateTransactionRequest,
    };
    use crate::version::IPC_PROTOCOL_VERSION;

    fn request(command: Command) -> RequestEnvelope {
        RequestEnvelope {
            protocol_version: IPC_PROTOCOL_VERSION,
            request_id: 1,
            session_token: Some("opaque-session-token".into()),
            command,
        }
    }

    fn create() -> CreateTransactionRequest {
        CreateTransactionRequest {
            name: "Groceries".into(),
            price: 10.0,
            description: "weekly".into(),
            datetime_unix_seconds: 1_800_000_000,
            category: "food".into(),
            is_recurring: false,
            recurring_frequency: None,
        }
    }

    fn update() -> UpdateTransactionRequest {
        UpdateTransactionRequest {
            id: 1,
            name: FieldChange::Unchanged,
            price: FieldChange::Set(12.0),
            description: FieldChange::Unchanged,
            datetime_unix_seconds: FieldChange::Unchanged,
            category: FieldChange::Unchanged,
            is_recurring: FieldChange::Unchanged,
            recurring_frequency: FieldChange::Unchanged,
        }
    }

    #[test]
    fn rejects_non_finite_create_amounts_and_invalid_recurrence_pairs() {
        let mut input = create();
        input.price = f64::NAN;
        assert_eq!(
            request(Command::CreateTransaction { transaction: input }).validate(),
            Err(RequestValidationError::InvalidTransactionPayload)
        );

        let mut input = create();
        input.is_recurring = true;
        assert_eq!(
            request(Command::CreateTransaction { transaction: input }).validate(),
            Err(RequestValidationError::InvalidTransactionPayload)
        );
    }

    #[test]
    fn accepts_valid_create_request() {
        assert_eq!(
            request(Command::CreateTransaction {
                transaction: create()
            })
            .validate(),
            Ok(())
        );
    }

    #[test]
    fn update_can_explicitly_clear_nullable_recurrence_frequency() {
        let mut input = update();
        input.is_recurring = FieldChange::Set(false);
        input.recurring_frequency = FieldChange::Clear;
        assert_eq!(
            request(Command::UpdateTransaction { transaction: input }).validate(),
            Ok(())
        );
    }

    #[test]
    fn disabling_recurrence_without_frequency_is_normalized_by_the_service() {
        let mut input = update();
        input.is_recurring = FieldChange::Set(false);
        assert_eq!(
            request(Command::UpdateTransaction { transaction: input }).validate(),
            Ok(())
        );
    }

    #[test]
    fn rejects_empty_update_and_clear_of_non_nullable_fields() {
        let empty = UpdateTransactionRequest {
            id: 1,
            name: FieldChange::Unchanged,
            price: FieldChange::Unchanged,
            description: FieldChange::Unchanged,
            datetime_unix_seconds: FieldChange::Unchanged,
            category: FieldChange::Unchanged,
            is_recurring: FieldChange::Unchanged,
            recurring_frequency: FieldChange::Unchanged,
        };
        assert_eq!(
            request(Command::UpdateTransaction { transaction: empty }).validate(),
            Err(RequestValidationError::InvalidTransactionPayload)
        );

        let mut clear_name = update();
        clear_name.name = FieldChange::Clear;
        assert_eq!(
            request(Command::UpdateTransaction {
                transaction: clear_name
            })
            .validate(),
            Err(RequestValidationError::InvalidTransactionPayload)
        );
    }

    #[test]
    fn enabling_recurrence_requires_an_explicit_schedule_anchor() {
        let mut input = update();
        input.is_recurring = FieldChange::Set(true);
        input.recurring_frequency = FieldChange::Set(RecurringFrequency::Monthly);
        assert_eq!(
            request(Command::UpdateTransaction {
                transaction: input.clone()
            })
            .validate(),
            Err(RequestValidationError::InvalidTransactionPayload)
        );

        input.datetime_unix_seconds = FieldChange::Set(1_800_000_000);
        assert_eq!(
            request(Command::UpdateTransaction { transaction: input }).validate(),
            Ok(())
        );
    }

    #[test]
    fn recurrence_frequency_cannot_be_set_without_enabling_recurrence() {
        let mut input = update();
        input.recurring_frequency = FieldChange::Set(RecurringFrequency::Weekly);
        assert_eq!(
            request(Command::UpdateTransaction { transaction: input }).validate(),
            Err(RequestValidationError::InvalidTransactionPayload)
        );
    }
}
