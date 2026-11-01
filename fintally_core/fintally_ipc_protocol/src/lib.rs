//! Versioned, transport-independent IPC message contract.
//!
//! Keep this crate limited to wire DTOs, envelopes, validation, and codecs.
//! It must not depend on application services, databases, UI frameworks, or
//! native compute engines.

pub mod codec;
pub mod command;
pub mod dto;
pub mod envelope;
pub mod error;
pub mod limits;
pub mod response;
pub mod validation;
pub mod version;

pub use codec::{decode_payload, encode_payload, CodecError};
pub use command::Command;
pub use dto::{
    BudgetPage, BudgetRecord, CreateBudgetRequest, CreateTransactionRequest, DeleteBudgetRequest,
    DeleteBudgetResult, DeleteTransactionResult, FieldChange, LoginResult, RecurringFrequency,
    TransactionPage, TransactionReceiptData, TransactionRecord, UpdateBudgetRequest,
    UpdateTransactionRequest,
};
pub use envelope::{RequestEnvelope, ResponseEnvelope};
pub use error::{ErrorCode, IpcError};
pub use limits::*;
pub use response::ResponsePayload;
pub use validation::RequestValidationError;
pub use version::*;
pub mod frame;

pub use frame::{
    decode_frame_header, encode_frame_header, FrameHeaderError, FRAME_FORMAT_VERSION, FRAME_MAGIC,
};

pub mod auth_limits;

pub use dto::auth::{AuthSession, IdentityProvider, LogoutResult, SessionStatus, StorageMode};
