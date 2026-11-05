//! Versioned commands accepted by the IPC service.
//!
//! The legacy `Login` variant remains for old clients. New UI flows must use
//! `LoginWithStorage`/`SignUp` so data residency is an explicit user choice.

use crate::dto::{
    CreateBudgetRequest, CreateTransactionRequest, DeleteBudgetRequest, StorageMode,
    UpdateBudgetRequest, UpdateTransactionRequest,
};
use kinetib_feedback_domain::FeedbackSubmissionRequest;
use serde::{Deserialize, Serialize};
use std::fmt;

#[derive(Clone, Serialize, Deserialize, PartialEq)]
#[serde(
    tag = "type",
    content = "payload",
    rename_all = "snake_case",
    deny_unknown_fields
)]
pub enum Command {
    /// Legacy login contract retained for migration compatibility. Do not use
    /// this variant for new UI flows because its storage policy is implicit.
    Login { email: String, password: String },
    /// Create an account in the explicitly selected local-only or cloud store.
    SignUp {
        email: String,
        password: String,
        storage_mode: StorageMode,
    },
    /// Sign in using email/password against the explicitly selected store.
    LoginWithStorage {
        email: String,
        password: String,
        storage_mode: StorageMode,
    },
    /// Submit a Google ID token for server-side validation, without embedding a
    /// Google client secret in the client. LocalOnly must not touch Supabase.
    GoogleSignIn {
        id_token: String,
        storage_mode: StorageMode,
    },
    /// Create/link a FinTally account from a verified Google identity.
    /// The kernel must reject an existing account if signup-only semantics apply.
    GoogleSignUp {
        id_token: String,
        storage_mode: StorageMode,
    },
    /// Revoke the active server-side session. Requires an envelope session token.
    Logout,
    /// Ask the kernel to validate this token against its session store.
    CheckSession,
    /// Submit feedback for the authenticated CloudSync principal.
    /// The server derives the owner from the envelope session token.
    SubmitFeedback {
        submission: FeedbackSubmissionRequest,
    },
    /// Request a bounded page of transaction records.
    ListTransactions { page: u32, page_size: u16 },
    /// Create a transaction in the session's explicitly selected storage scope.
    CreateTransaction {
        transaction: CreateTransactionRequest,
    },
    /// Update only explicit field changes; `Clear` is valid only for nullable fields.
    UpdateTransaction {
        transaction: UpdateTransactionRequest,
    },
    /// Delete one transaction owned by the authenticated session principal.
    DeleteTransaction { transaction_id: i64 },
    /// Fetch receipt rendering data, scoped to the transaction owner. This returns
    /// metadata only, not an arbitrary file or filesystem path.
    GetTransactionReceipt { transaction_id: i64 },
    /// List the current authenticated principal's budget allocations.
    ListBudgets,
    /// Create an allocation and its initial threshold rules atomically.
    CreateBudget { budget: CreateBudgetRequest },
    /// Update an allocation only when its revision matches the client's revision.
    UpdateBudget { budget: UpdateBudgetRequest },
    /// Delete an allocation only when its revision matches the client's revision.
    DeleteBudget { budget: DeleteBudgetRequest },
}

impl fmt::Debug for Command {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Login { .. } => formatter
                .debug_struct("Login")
                .field("email", &"[REDACTED]")
                .field("password", &"[REDACTED]")
                .finish(),
            Self::SignUp { storage_mode, .. } => formatter
                .debug_struct("SignUp")
                .field("email", &"[REDACTED]")
                .field("password", &"[REDACTED]")
                .field("storage_mode", storage_mode)
                .finish(),
            Self::LoginWithStorage { storage_mode, .. } => formatter
                .debug_struct("LoginWithStorage")
                .field("email", &"[REDACTED]")
                .field("password", &"[REDACTED]")
                .field("storage_mode", storage_mode)
                .finish(),
            Self::GoogleSignIn { storage_mode, .. } => formatter
                .debug_struct("GoogleSignIn")
                .field("id_token", &"[REDACTED]")
                .field("storage_mode", storage_mode)
                .finish(),
            Self::GoogleSignUp { storage_mode, .. } => formatter
                .debug_struct("GoogleSignUp")
                .field("id_token", &"[REDACTED]")
                .field("storage_mode", storage_mode)
                .finish(),
            Self::Logout => formatter.write_str("Logout"),
            Self::CheckSession => formatter.write_str("CheckSession"),
            Self::SubmitFeedback { submission } => formatter
                .debug_struct("SubmitFeedback")
                .field("submission_id", &submission.submission_id)
                .field("submission", &"[REDACTED]")
                .finish(),
            Self::ListTransactions { page, page_size } => formatter
                .debug_struct("ListTransactions")
                .field("page", page)
                .field("page_size", page_size)
                .finish(),
            Self::CreateTransaction { .. } => formatter
                .debug_struct("CreateTransaction")
                .field("transaction", &"[REDACTED]")
                .finish(),
            Self::UpdateTransaction { transaction } => formatter
                .debug_struct("UpdateTransaction")
                .field("transaction_id", &transaction.id)
                .field("changes", &"[REDACTED]")
                .finish(),
            Self::DeleteTransaction { transaction_id }
            | Self::GetTransactionReceipt { transaction_id } => formatter
                .debug_struct(match self {
                    Self::DeleteTransaction { .. } => "DeleteTransaction",
                    _ => "GetTransactionReceipt",
                })
                .field("transaction_id", transaction_id)
                .finish(),
            Self::ListBudgets => formatter.write_str("ListBudgets"),
            Self::CreateBudget { .. } => formatter
                .debug_struct("CreateBudget")
                .field("budget", &"[REDACTED]")
                .finish(),
            Self::UpdateBudget { budget } => formatter
                .debug_struct("UpdateBudget")
                .field("budget_id", &budget.id)
                .field("changes", &"[REDACTED]")
                .finish(),
            Self::DeleteBudget { budget } => formatter
                .debug_struct("DeleteBudget")
                .field("budget_id", &budget.id)
                .field("expected_revision", &budget.expected_revision)
                .finish(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Command;
    use crate::dto::{
        CreateTransactionRequest, FieldChange, StorageMode, UpdateTransactionRequest,
    };

    #[test]
    fn command_serde_round_trip_preserves_storage_policy() {
        let command = Command::LoginWithStorage {
            email: "person@example.com".to_owned(),
            password: "example-password".to_owned(),
            storage_mode: StorageMode::LocalOnly,
        };
        let encoded = serde_json::to_string(&command).expect("serialize command");
        assert!(encoded.contains("local_only"));
        let decoded: Command = serde_json::from_str(&encoded).expect("deserialize command");
        assert_eq!(decoded, command);
    }

    #[test]
    fn budget_command_debug_redacts_amount_and_category() {
        let command = Command::CreateBudget {
            budget: crate::dto::CreateBudgetRequest {
                category: "private-category".into(),
                amount_minor_units: 987_654,
                period_start_unix: 1_800_000_000,
                period_end_unix: None,
            },
        };
        let debug = format!("{command:?}");
        assert!(debug.contains("REDACTED"));
        assert!(!debug.contains("private-category"));
        assert!(!debug.contains("987654"));
    }

    #[test]
    fn transaction_debug_does_not_print_financial_data() {
        let command = Command::CreateTransaction {
            transaction: CreateTransactionRequest {
                name: "private merchant".into(),
                price: 9876.54,
                description: "private transaction".into(),
                datetime_unix_seconds: 1_800_000_000,
                category: "private category".into(),
                is_recurring: false,
                recurring_frequency: None,
            },
        };
        let debug = format!("{command:?}");
        assert!(!debug.contains("private merchant"));
        assert!(!debug.contains("9876.54"));
        assert!(!debug.contains("private transaction"));
    }

    #[test]
    fn update_command_keeps_nullable_clear_explicit() {
        let command = Command::UpdateTransaction {
            transaction: UpdateTransactionRequest {
                id: 5,
                name: FieldChange::Unchanged,
                price: FieldChange::Unchanged,
                description: FieldChange::Unchanged,
                datetime_unix_seconds: FieldChange::Unchanged,
                category: FieldChange::Unchanged,
                is_recurring: FieldChange::Set(false),
                recurring_frequency: FieldChange::Clear,
            },
        };
        let encoded = serde_json::to_string(&command).unwrap();
        assert!(encoded.contains("\"state\":\"clear\""));
        assert_eq!(serde_json::from_str::<Command>(&encoded).unwrap(), command);
    }

    #[test]
    fn feedback_command_debug_redacts_user_content_and_contact_details() {
        use kinetib_feedback_domain::{
            FeedbackCategory, FeedbackPlatform, FeedbackSubmissionRequest,
        };

        let request = FeedbackSubmissionRequest::new(
            FeedbackCategory::Support,
            Some("private title".into()),
            "private feedback body".into(),
            None,
            Some("person@example.test".into()),
            true,
            FeedbackPlatform::Linux,
            "0.5.2".into(),
            false,
            None,
        );
        let debug = format!(
            "{:?}",
            Command::SubmitFeedback {
                submission: request
            }
        );
        assert!(!debug.contains("private title"));
        assert!(!debug.contains("private feedback body"));
        assert!(!debug.contains("person@example.test"));
        assert!(debug.contains("REDACTED"));
    }
}
