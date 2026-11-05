//! Transport-independent feedback contracts and bounded validation.
//!
//! This crate is shared by the IPC protocol, application service, and native/WebAssembly clients.
//! It intentionally contains no database, UI, transport, or platform dependencies.

mod request;
mod types;
mod validation;

pub use request::{FeedbackSubmissionRequest, ValidatedFeedback};
pub use types::{
    DeviceClass, FeedbackCategory, FeedbackDiagnostics, FeedbackFeature, FeedbackPlatform,
    FeedbackReceipt,
};
pub use validation::FeedbackValidationError;
