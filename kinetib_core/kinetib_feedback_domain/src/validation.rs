use thiserror::Error;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum FeedbackValidationError {
    #[error("invalid submission identifier")]
    InvalidSubmissionId,
    #[error("feedback message is required")]
    EmptyMessage,
    #[error("feedback message exceeds the size limit")]
    MessageTooLong,
    #[error("feedback text contains unsupported control characters")]
    InvalidText,
    #[error("feedback title is invalid")]
    InvalidTitle,
    #[error("experience rating must be between 1 and 5")]
    InvalidRating,
    #[error("contact permission is required before storing an email address")]
    ContactConsentRequired,
    #[error("contact name is invalid")]
    InvalidContactName,
    #[error("contact email is required when contact is requested")]
    ContactEmailRequired,
    #[error("contact email is invalid")]
    InvalidContactEmail,
    #[error("diagnostics require explicit consent")]
    DiagnosticsConsentRequired,
    #[error("application version is invalid")]
    InvalidAppVersion,
    #[error("diagnostics metadata is invalid")]
    InvalidDiagnostics,
}
