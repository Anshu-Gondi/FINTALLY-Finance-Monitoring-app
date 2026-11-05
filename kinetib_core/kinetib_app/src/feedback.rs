//! Cloud feedback application service.
//!
//! The service accepts the shared domain request, validates it, and delegates
//! persistence to the PostgreSQL repository. It is intentionally independent
//! of Axum, Slint, IPC and platform transports.

use crate::auth::AuthenticatedPrincipal;
use kinetib_db::feedback::{FeedbackRepository, FeedbackRepositoryError};
use kinetib_db::DbContext;
use kinetib_feedback_domain::{
    FeedbackReceipt, FeedbackSubmissionRequest, FeedbackValidationError,
};
use thiserror::Error;

#[derive(Clone)]
pub struct FeedbackService {
    repository: FeedbackRepository,
}

#[derive(Debug, Error)]
pub enum FeedbackServiceError {
    #[error(transparent)]
    Validation(#[from] FeedbackValidationError),
    #[error(transparent)]
    Persistence(#[from] FeedbackRepositoryError),
}

impl FeedbackService {
    pub fn new(db: &DbContext) -> Self {
        Self {
            repository: FeedbackRepository::new(db.pool.clone()),
        }
    }

    pub async fn submit(
        &self,
        principal: &AuthenticatedPrincipal,
        request: &FeedbackSubmissionRequest,
    ) -> Result<FeedbackReceipt, FeedbackServiceError> {
        let validated = request.validate()?;
        Ok(self
            .repository
            .submit(principal.user_id(), &validated)
            .await?)
    }
}
