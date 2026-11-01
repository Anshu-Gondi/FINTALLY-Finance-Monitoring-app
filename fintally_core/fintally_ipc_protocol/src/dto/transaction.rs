//! Transaction-related IPC data transfer objects.
//!
//! These are transport representations, not database models. Conversion from
//! a database row or domain object belongs in the service/adapter layer.

use serde::{Deserialize, Serialize};

/// Frequency of a recurring transaction.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum RecurringFrequency {
    Daily,
    Weekly,
    Monthly,
}

/// A tri-state change for an update request.
///
/// `unchanged` preserves the stored value, `set` replaces it, and `clear`
/// explicitly clears a nullable value. The tagged representation avoids the
/// ambiguity between omitted JSON fields and explicit nulls.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(
    tag = "state",
    content = "value",
    rename_all = "snake_case",
    deny_unknown_fields
)]
pub enum FieldChange<T> {
    Unchanged,
    Set(T),
    Clear,
}

impl<T> Default for FieldChange<T> {
    fn default() -> Self {
        Self::Unchanged
    }
}

/// A validated-at-the-boundary create request. `price` remains `f64` for wire
/// compatibility; the application rejects NaN and infinities before storage.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct CreateTransactionRequest {
    pub name: String,
    pub price: f64,
    pub description: String,
    /// Unix timestamp in whole seconds, preserving the legacy database contract.
    pub datetime_unix_seconds: i64,
    pub category: String,
    pub is_recurring: bool,
    pub recurring_frequency: Option<RecurringFrequency>,
}

/// A partial update request. Only `recurring_frequency` is nullable in the
/// current transaction schema; `Clear` is rejected for the other fields.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct UpdateTransactionRequest {
    pub id: i64,
    #[serde(default)]
    pub name: FieldChange<String>,
    #[serde(default)]
    pub price: FieldChange<f64>,
    #[serde(default)]
    pub description: FieldChange<String>,
    #[serde(default)]
    pub datetime_unix_seconds: FieldChange<i64>,
    #[serde(default)]
    pub category: FieldChange<String>,
    #[serde(default)]
    pub is_recurring: FieldChange<bool>,
    #[serde(default)]
    pub recurring_frequency: FieldChange<RecurringFrequency>,
}

/// Canonical transaction data returned to the client UI.
///
/// `receipt_url` is retained for legacy response compatibility. The kernel must
/// never use it as a filesystem path; receipt access is an authenticated
/// transaction-scoped operation.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct TransactionRecord {
    pub id: i64,
    pub name: String,
    pub price: f64,
    pub description: String,
    pub datetime_unix_seconds: i64,
    pub category: String,
    pub is_recurring: bool,
    pub recurring_frequency: Option<RecurringFrequency>,
    pub receipt_url: Option<String>,
}

/// A bounded page of transaction records.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct TransactionPage {
    pub page: u32,
    pub page_size: u16,
    pub has_more: bool,
    pub items: Vec<TransactionRecord>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct DeleteTransactionResult {
    pub transaction_id: i64,
    pub deleted: bool,
}

/// Data needed to render the legacy PDF receipt without exposing a database
/// handle or a path. PDF rendering/transfer can be layered on a bounded receipt
/// transfer protocol separately from transaction metadata requests.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct TransactionReceiptData {
    pub transaction_id: i64,
    pub name: String,
    pub price: f64,
    pub description: String,
    pub datetime_unix_seconds: i64,
    pub category: String,
}

#[cfg(test)]
mod tests {
    use super::{
        CreateTransactionRequest, FieldChange, RecurringFrequency, TransactionPage,
        TransactionRecord, UpdateTransactionRequest,
    };

    #[test]
    fn recurring_frequency_uses_stable_snake_case_values() {
        let encoded = serde_json::to_string(&RecurringFrequency::Monthly).unwrap();
        assert_eq!(encoded, r#""monthly""#);
    }

    #[test]
    fn transaction_dto_round_trips() {
        let page = TransactionPage {
            page: 1,
            page_size: 1,
            has_more: false,
            items: vec![TransactionRecord {
                id: 7,
                name: "Groceries".to_owned(),
                price: 12.5,
                description: "Weekly shopping".to_owned(),
                datetime_unix_seconds: 1_800_000_000,
                category: "food".to_owned(),
                is_recurring: true,
                recurring_frequency: Some(RecurringFrequency::Weekly),
                receipt_url: None,
            }],
        };

        let encoded = serde_json::to_string(&page).unwrap();
        let decoded: TransactionPage = serde_json::from_str(&encoded).unwrap();
        assert_eq!(decoded, page);
    }

    #[test]
    fn update_fields_preserve_three_distinct_states() {
        let update = UpdateTransactionRequest {
            id: 4,
            name: FieldChange::Unchanged,
            price: FieldChange::Set(10.5),
            description: FieldChange::Clear,
            datetime_unix_seconds: FieldChange::Unchanged,
            category: FieldChange::Unchanged,
            is_recurring: FieldChange::Set(false),
            recurring_frequency: FieldChange::Clear,
        };
        let json = serde_json::to_string(&update).unwrap();
        let restored: UpdateTransactionRequest = serde_json::from_str(&json).unwrap();
        assert_eq!(restored, update);
        assert!(json.contains("\"state\":\"unchanged\""));
        assert!(json.contains("\"state\":\"clear\""));
    }

    #[test]
    fn create_request_retains_unix_timestamp_and_float_amount_contract() {
        let request = CreateTransactionRequest {
            name: "Groceries".into(),
            price: 12.5,
            description: String::new(),
            datetime_unix_seconds: 1_800_000_000,
            category: "food".into(),
            is_recurring: false,
            recurring_frequency: None,
        };
        let encoded = serde_json::to_string(&request).unwrap();
        let restored: CreateTransactionRequest = serde_json::from_str(&encoded).unwrap();
        assert_eq!(restored, request);
    }
}
