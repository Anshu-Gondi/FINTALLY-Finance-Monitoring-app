use kinetib_feedback_domain::{FeedbackDiagnostics, FeedbackReceipt, ValidatedFeedback};
use sha2::{Digest, Sha256};
use sqlx::PgPool;
use thiserror::Error;

#[derive(Clone)]
pub struct FeedbackRepository {
    pool: PgPool,
}

#[derive(Debug, Error)]
pub enum FeedbackRepositoryError {
    #[error("feedback database operation failed")]
    Database(#[source] sqlx::Error),
    #[error("feedback payload could not be serialized")]
    Serialization,
    #[error("feedback owner is invalid")]
    InvalidOwner,
    #[error("submission identifier was reused for different feedback")]
    IdempotencyConflict,
}

impl FeedbackRepository {
    pub fn new(pool: PgPool) -> Self {
        Self { pool }
    }

    /// Inserts a feedback record once. Repeating the same UUID and payload is
    /// safe; reusing the UUID with a different payload returns a conflict.
    pub async fn submit(
        &self,
        user_id: i64,
        feedback: &ValidatedFeedback,
    ) -> Result<FeedbackReceipt, FeedbackRepositoryError> {
        if user_id <= 0 {
            return Err(FeedbackRepositoryError::InvalidOwner);
        }
        let request_hash = request_fingerprint(feedback)?;
        let diagnostics = feedback
            .diagnostics
            .as_ref()
            .map(serde_json::to_value)
            .transpose()
            .map_err(|_| FeedbackRepositoryError::Serialization)?;

        let inserted = sqlx::query!(
            r#"
            INSERT INTO feedback_submissions (
                user_id,
                submission_id,
                request_hash,
                category,
                affected_feature,
                title,
                message,
                experience_rating,
                contact_name,
                contact_email,
                contact_consent,
                platform,
                app_version,
                diagnostics_consent,
                diagnostics_json
            )
            VALUES (
                $1, $2, $3, $4, $5, $6, $7, $8, $9, $10,
                $11, $12, $13, $14, $15::jsonb
            )
            ON CONFLICT (user_id, submission_id) DO NOTHING
            RETURNING submission_id
            "#,
            user_id,
            feedback.submission_id,
            request_hash.as_str(),
            feedback.category.as_str(),
            feedback.affected_feature.map(|feature| feature.as_str()),
            feedback.title.as_deref(),
            feedback.message.as_str(),
            feedback.experience_rating,
            feedback.contact_name.as_deref(),
            feedback.contact_email.as_deref(),
            feedback.contact_consent,
            feedback.platform.as_str(),
            feedback.app_version.as_str(),
            feedback.diagnostics_consent,
            diagnostics,
        )
        .fetch_optional(&self.pool)
        .await
        .map_err(FeedbackRepositoryError::Database)?;

        if inserted.is_some() {
            return Ok(FeedbackReceipt {
                submission_id: feedback.submission_id,
                duplicate: false,
            });
        }

        let existing = sqlx::query!(
            r#"
            SELECT request_hash
            FROM feedback_submissions
            WHERE user_id = $1 AND submission_id = $2
            "#,
            user_id,
            feedback.submission_id
        )
        .fetch_one(&self.pool)
        .await
        .map_err(FeedbackRepositoryError::Database)?;

        if existing.request_hash != request_hash {
            return Err(FeedbackRepositoryError::IdempotencyConflict);
        }

        Ok(FeedbackReceipt {
            submission_id: feedback.submission_id,
            duplicate: true,
        })
    }
}

fn request_fingerprint(feedback: &ValidatedFeedback) -> Result<String, FeedbackRepositoryError> {
    let mut hasher = Sha256::new();
    hasher.update(feedback.submission_id.as_bytes());

    update_field(&mut hasher, feedback.category.as_str().as_bytes());
    if let Some(feature) = feedback.affected_feature {
        update_optional(&mut hasher, Some(feature.as_str()));
    } else {
        update_optional(&mut hasher, None);
    }
    update_optional(&mut hasher, feedback.title.as_deref());
    update_field(&mut hasher, feedback.message.as_bytes());
    let rating = feedback.experience_rating.map(|value| value.to_string());
    update_optional(&mut hasher, rating.as_deref());
    update_optional(&mut hasher, feedback.contact_name.as_deref());
    update_optional(&mut hasher, feedback.contact_email.as_deref());
    hasher.update([u8::from(feedback.contact_consent)]);
    update_field(&mut hasher, feedback.platform.as_str().as_bytes());
    update_field(&mut hasher, feedback.app_version.as_bytes());
    hasher.update([u8::from(feedback.diagnostics_consent)]);

    match feedback.diagnostics.as_ref() {
        Some(diagnostics) => {
            hasher.update([1]);
            update_diagnostics(&mut hasher, diagnostics);
        }
        None => hasher.update([0]),
    }

    let digest = hasher.finalize();
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut output = String::with_capacity(64);
    for byte in digest {
        output.push(HEX[(byte >> 4) as usize] as char);
        output.push(HEX[(byte & 0x0f) as usize] as char);
    }
    Ok(output)
}

fn update_diagnostics(hasher: &mut Sha256, diagnostics: &FeedbackDiagnostics) {
    update_field(hasher, diagnostics.device_class.as_str().as_bytes());
    update_optional(hasher, diagnostics.os_version.as_deref());
    update_optional(hasher, diagnostics.locale.as_deref());
}

fn update_optional(hasher: &mut Sha256, value: Option<&str>) {
    match value {
        Some(value) => {
            hasher.update([1]);
            update_field(hasher, value.as_bytes());
        }
        None => hasher.update([0]),
    }
}

fn update_field(hasher: &mut Sha256, value: &[u8]) {
    hasher.update((value.len() as u64).to_be_bytes());
    hasher.update(value);
}
