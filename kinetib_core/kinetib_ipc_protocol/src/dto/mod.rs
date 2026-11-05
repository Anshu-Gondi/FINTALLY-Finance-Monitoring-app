//! Bounded, database-independent IPC DTOs.

pub mod auth;
pub mod budget;
pub mod transaction;

pub use auth::{
    AuthSession, IdentityProvider, LoginResult, LogoutResult, SessionStatus, StorageMode,
};
pub use budget::{
    BudgetPage, BudgetRecord, CreateBudgetRequest, DeleteBudgetRequest, DeleteBudgetResult,
    UpdateBudgetRequest,
};
pub use transaction::{
    CreateTransactionRequest, DeleteTransactionResult, FieldChange, RecurringFrequency,
    TransactionPage, TransactionReceiptData, TransactionRecord, UpdateTransactionRequest,
};
