//! PostgreSQL persistence for cloud feedback submissions.

mod repository;

pub use repository::{FeedbackRepository, FeedbackRepositoryError};
