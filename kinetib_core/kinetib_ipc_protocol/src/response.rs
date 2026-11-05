//! Typed successful IPC response payloads.

use crate::dto::{
    AuthSession, BudgetPage, BudgetRecord, DeleteBudgetResult, DeleteTransactionResult,
    LoginResult, LogoutResult, SessionStatus, TransactionPage, TransactionReceiptData,
    TransactionRecord,
};
use kinetib_feedback_domain::FeedbackReceipt;
use serde::{Deserialize, Serialize};
use std::fmt;

#[derive(Clone, Serialize, Deserialize, PartialEq)]
#[serde(tag = "type", content = "payload", rename_all = "snake_case")]
pub enum ResponsePayload {
    /// Legacy response for the legacy `Command::Login` operation.
    Login(LoginResult),
    /// Successful response to SignUp, LoginWithStorage, or GoogleSignIn.
    Authenticated(AuthSession),
    /// Result of a CheckSession request.
    SessionStatus(SessionStatus),
    /// Receipt for an accepted authenticated feedback submission.
    FeedbackSubmitted(FeedbackReceipt),
    /// Server acknowledged logout; `revoked=false` means already expired/revoked.
    LoggedOut(LogoutResult),
    Transactions(TransactionPage),
    TransactionCreated(TransactionRecord),
    TransactionUpdated(TransactionRecord),
    TransactionDeleted(DeleteTransactionResult),
    TransactionReceipt(TransactionReceiptData),
    Budgets(BudgetPage),
    BudgetCreated(BudgetRecord),
    BudgetUpdated(BudgetRecord),
    BudgetDeleted(DeleteBudgetResult),
}

impl fmt::Debug for ResponsePayload {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Login(result) => formatter.debug_tuple("Login").field(result).finish(),
            Self::Authenticated(session) => formatter
                .debug_struct("Authenticated")
                .field("session_token", &"[REDACTED]")
                .field("storage_mode", &session.storage_mode)
                .field("provider", &session.provider)
                .field("expires_at_unix_seconds", &session.expires_at_unix_seconds)
                .finish(),
            Self::SessionStatus(status) => formatter
                .debug_tuple("SessionStatus")
                .field(status)
                .finish(),
            Self::FeedbackSubmitted(receipt) => formatter
                .debug_struct("FeedbackSubmitted")
                .field("submission_id", &receipt.submission_id)
                .field("duplicate", &receipt.duplicate)
                .finish(),
            Self::LoggedOut(result) => formatter.debug_tuple("LoggedOut").field(result).finish(),
            Self::Transactions(page) => formatter
                .debug_struct("Transactions")
                .field("page", &page.page)
                .field("page_size", &page.page_size)
                .field("has_more", &page.has_more)
                .field("item_count", &page.items.len())
                .finish(),
            Self::TransactionCreated(record) => formatter
                .debug_struct("TransactionCreated")
                .field("transaction_id", &record.id)
                .field("record", &"[REDACTED]")
                .finish(),
            Self::TransactionUpdated(record) => formatter
                .debug_struct("TransactionUpdated")
                .field("transaction_id", &record.id)
                .field("record", &"[REDACTED]")
                .finish(),
            Self::TransactionDeleted(result) => formatter
                .debug_tuple("TransactionDeleted")
                .field(result)
                .finish(),
            Self::TransactionReceipt(receipt) => formatter
                .debug_struct("TransactionReceipt")
                .field("transaction_id", &receipt.transaction_id)
                .field("data", &"[REDACTED]")
                .finish(),
            Self::Budgets(page) => formatter
                .debug_struct("Budgets")
                .field("item_count", &page.items.len())
                .field("records", &"[REDACTED]")
                .finish(),
            Self::BudgetCreated(record) => formatter
                .debug_struct("BudgetCreated")
                .field("budget_id", &record.id)
                .field("record", &"[REDACTED]")
                .finish(),
            Self::BudgetUpdated(record) => formatter
                .debug_struct("BudgetUpdated")
                .field("budget_id", &record.id)
                .field("record", &"[REDACTED]")
                .finish(),
            Self::BudgetDeleted(result) => formatter
                .debug_tuple("BudgetDeleted")
                .field(result)
                .finish(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::ResponsePayload;
    use crate::dto::{
        DeleteTransactionResult, RecurringFrequency, TransactionPage, TransactionReceiptData,
        TransactionRecord,
    };

    #[test]
    fn transaction_payload_debug_does_not_print_financial_records() {
        let payload = ResponsePayload::Transactions(TransactionPage {
            page: 1,
            page_size: 1,
            has_more: false,
            items: vec![TransactionRecord {
                id: 7,
                name: "private-merchant-name".into(),
                price: 123.45,
                description: "private-transaction-description".into(),
                datetime_unix_seconds: 1_800_000_000,
                category: "private-category".into(),
                is_recurring: false,
                recurring_frequency: Some(RecurringFrequency::Monthly),
                receipt_url: Some("https://private.example/receipt".into()),
            }],
        });

        let debug = format!("{payload:?}");
        assert!(debug.contains("item_count"));
        assert!(!debug.contains("private-merchant-name"));
        assert!(!debug.contains("private-transaction-description"));
        assert!(!debug.contains("123.45"));
        assert!(!debug.contains("private-category"));
        assert!(!debug.contains("private.example"));
    }

    #[test]
    fn receipt_response_debug_redacts_transaction_fields() {
        let payload = ResponsePayload::TransactionReceipt(TransactionReceiptData {
            transaction_id: 42,
            name: "private merchant".into(),
            price: 99.99,
            description: "private note".into(),
            datetime_unix_seconds: 1_800_000_000,
            category: "private category".into(),
        });
        let debug = format!("{payload:?}");
        assert!(debug.contains("transaction_id"));
        assert!(!debug.contains("private merchant"));
        assert!(!debug.contains("99.99"));
    }

    #[test]
    fn delete_result_round_trips() {
        let payload = ResponsePayload::TransactionDeleted(DeleteTransactionResult {
            transaction_id: 15,
            deleted: true,
        });
        let bytes = serde_json::to_vec(&payload).unwrap();
        assert_eq!(
            serde_json::from_slice::<ResponsePayload>(&bytes).unwrap(),
            payload
        );
    }
}
