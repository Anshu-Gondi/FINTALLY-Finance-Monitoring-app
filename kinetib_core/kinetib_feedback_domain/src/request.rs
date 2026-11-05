use serde::{Deserialize, Serialize};
use std::fmt;
use uuid::Uuid;

use crate::{
    FeedbackCategory, FeedbackDiagnostics, FeedbackFeature, FeedbackPlatform,
    FeedbackValidationError,
};

const MAX_MESSAGE_CHARS: usize = 5_000;
const MAX_MESSAGE_BYTES: usize = 6_000;
const MAX_TITLE_BYTES: usize = 120;
const MAX_CONTACT_NAME_BYTES: usize = 120;

/// Client request carried inside an authenticated FinTally IPC envelope.
/// The session token and authenticated user identity are never request fields.
#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FeedbackSubmissionRequest {
    pub submission_id: Uuid,
    pub category: FeedbackCategory,
    pub affected_feature: Option<FeedbackFeature>,
    pub title: Option<String>,
    pub message: String,
    pub experience_rating: Option<i16>,
    pub contact_name: Option<String>,
    pub contact_email: Option<String>,
    pub contact_consent: bool,
    pub platform: FeedbackPlatform,
    pub app_version: String,
    pub diagnostics_consent: bool,
    pub diagnostics: Option<FeedbackDiagnostics>,
}

impl FeedbackSubmissionRequest {
    /// Generate one idempotency key when the user creates a draft. Retain the
    /// same request and key when retrying a timed-out submission.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        category: FeedbackCategory,
        title: Option<String>,
        message: String,
        experience_rating: Option<i16>,
        contact_email: Option<String>,
        contact_consent: bool,
        platform: FeedbackPlatform,
        app_version: String,
        diagnostics_consent: bool,
        diagnostics: Option<FeedbackDiagnostics>,
    ) -> Self {
        Self {
            submission_id: Uuid::new_v4(),
            category,
            affected_feature: None,
            title,
            message,
            experience_rating,
            contact_name: None,
            contact_email,
            contact_consent,
            platform,
            app_version,
            diagnostics_consent,
            diagnostics,
        }
    }

    /// Attach optional UI context without changing the idempotency key.
    pub fn with_context(
        mut self,
        affected_feature: Option<FeedbackFeature>,
        contact_name: Option<String>,
    ) -> Self {
        self.affected_feature = affected_feature;
        self.contact_name = contact_name;
        self
    }

    pub fn validate(&self) -> Result<ValidatedFeedback, FeedbackValidationError> {
        if self.submission_id.is_nil() {
            return Err(FeedbackValidationError::InvalidSubmissionId);
        }

        let message = self.message.trim();
        if message.is_empty() {
            return Err(FeedbackValidationError::EmptyMessage);
        }
        if message.chars().count() > MAX_MESSAGE_CHARS || message.len() > MAX_MESSAGE_BYTES {
            return Err(FeedbackValidationError::MessageTooLong);
        }
        if contains_disallowed_control(message) {
            return Err(FeedbackValidationError::InvalidText);
        }

        let title = match self.title.as_deref() {
            Some(value) => {
                let value = value.trim();
                if value.is_empty() {
                    None
                } else {
                    if value.len() > MAX_TITLE_BYTES
                        || value.chars().count() > 120
                        || contains_disallowed_control(value)
                    {
                        return Err(FeedbackValidationError::InvalidTitle);
                    }
                    Some(value.to_owned())
                }
            }
            None => None,
        };

        if let Some(rating) = self.experience_rating {
            if !(1..=5).contains(&rating) {
                return Err(FeedbackValidationError::InvalidRating);
            }
        }

        let contact_name = match self.contact_name.as_deref() {
            Some(value) => {
                let value = value.trim();
                if !self.contact_consent {
                    return Err(FeedbackValidationError::ContactConsentRequired);
                }
                if value.is_empty()
                    || value.len() > MAX_CONTACT_NAME_BYTES
                    || contains_disallowed_control(value)
                {
                    return Err(FeedbackValidationError::InvalidContactName);
                }
                Some(value.to_owned())
            }
            None => None,
        };

        let contact_email = match self.contact_email.as_deref() {
            Some(value) => {
                let value = value.trim();
                if !self.contact_consent {
                    return Err(FeedbackValidationError::ContactConsentRequired);
                }
                if !valid_email_shape(value) {
                    return Err(FeedbackValidationError::InvalidContactEmail);
                }
                Some(value.to_owned())
            }
            None => {
                if self.contact_consent {
                    return Err(FeedbackValidationError::ContactEmailRequired);
                }
                None
            }
        };

        if self.diagnostics.is_some() && !self.diagnostics_consent {
            return Err(FeedbackValidationError::DiagnosticsConsentRequired);
        }

        let app_version = self.app_version.trim();
        if app_version.is_empty()
            || app_version.len() > 64
            || contains_disallowed_control(app_version)
        {
            return Err(FeedbackValidationError::InvalidAppVersion);
        }

        let diagnostics = self
            .diagnostics
            .as_ref()
            .map(validate_diagnostics)
            .transpose()?;

        Ok(ValidatedFeedback {
            submission_id: self.submission_id,
            category: self.category,
            affected_feature: self.affected_feature,
            title,
            message: message.to_owned(),
            experience_rating: self.experience_rating,
            contact_name,
            contact_email,
            contact_consent: self.contact_consent,
            platform: self.platform,
            app_version: app_version.to_owned(),
            diagnostics_consent: self.diagnostics_consent,
            diagnostics,
        })
    }
}

impl fmt::Debug for FeedbackSubmissionRequest {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("FeedbackSubmissionRequest")
            .field("submission_id", &self.submission_id)
            .field("category", &self.category)
            .field("affected_feature", &self.affected_feature)
            .field("title", &self.title.as_ref().map(|_| "[REDACTED]"))
            .field("message", &"[REDACTED]")
            .field("experience_rating", &self.experience_rating)
            .field(
                "contact_name",
                &self.contact_name.as_ref().map(|_| "[REDACTED]"),
            )
            .field(
                "contact_email",
                &self.contact_email.as_ref().map(|_| "[REDACTED]"),
            )
            .field("contact_consent", &self.contact_consent)
            .field("platform", &self.platform)
            .field("app_version", &self.app_version)
            .field("diagnostics_consent", &self.diagnostics_consent)
            .field(
                "diagnostics",
                &self.diagnostics.as_ref().map(|_| "[REDACTED]"),
            )
            .finish()
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ValidatedFeedback {
    pub submission_id: Uuid,
    pub category: FeedbackCategory,
    pub affected_feature: Option<FeedbackFeature>,
    pub title: Option<String>,
    pub message: String,
    pub experience_rating: Option<i16>,
    pub contact_name: Option<String>,
    pub contact_email: Option<String>,
    pub contact_consent: bool,
    pub platform: FeedbackPlatform,
    pub app_version: String,
    pub diagnostics_consent: bool,
    pub diagnostics: Option<FeedbackDiagnostics>,
}

fn validate_diagnostics(
    diagnostics: &FeedbackDiagnostics,
) -> Result<FeedbackDiagnostics, FeedbackValidationError> {
    let os_version = clean_optional_metadata(diagnostics.os_version.as_deref(), 128)?;
    let locale = clean_optional_metadata(diagnostics.locale.as_deref(), 35)?;

    if let Some(locale_value) = locale.as_deref() {
        if !locale_value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-' || byte == b'_')
        {
            return Err(FeedbackValidationError::InvalidDiagnostics);
        }
    }

    Ok(FeedbackDiagnostics {
        device_class: diagnostics.device_class,
        os_version,
        locale,
    })
}

fn clean_optional_metadata(
    value: Option<&str>,
    max_bytes: usize,
) -> Result<Option<String>, FeedbackValidationError> {
    let Some(value) = value else {
        return Ok(None);
    };
    let value = value.trim();
    if value.is_empty() {
        return Ok(None);
    }
    if value.len() > max_bytes || contains_disallowed_control(value) {
        return Err(FeedbackValidationError::InvalidDiagnostics);
    }
    Ok(Some(value.to_owned()))
}

fn contains_disallowed_control(value: &str) -> bool {
    value
        .chars()
        .any(|character| character.is_control() && !matches!(character, '\n' | '\r' | '\t'))
}

fn valid_email_shape(value: &str) -> bool {
    if value.is_empty() || value.len() > 254 || value.chars().any(char::is_whitespace) {
        return false;
    }
    let mut parts = value.split('@');
    let Some(local) = parts.next() else {
        return false;
    };
    let Some(domain) = parts.next() else {
        return false;
    };
    if parts.next().is_some() || local.is_empty() || domain.is_empty() {
        return false;
    }
    domain.contains('.')
        && !domain.starts_with('.')
        && !domain.ends_with('.')
        && !domain.contains("..")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::DeviceClass;

    fn valid_request() -> FeedbackSubmissionRequest {
        FeedbackSubmissionRequest::new(
            FeedbackCategory::General,
            Some("A useful suggestion".into()),
            "Please improve the budget overview.".into(),
            Some(4),
            None,
            false,
            FeedbackPlatform::Linux,
            "0.5.2".into(),
            false,
            None,
        )
    }

    #[test]
    fn validates_and_trims_feedback_text() {
        let mut request = valid_request();
        request.message = "  Please improve the budget overview.  ".into();
        let validated = request.validate().expect("valid request");
        assert_eq!(validated.message, "Please improve the budget overview.");
    }

    #[test]
    fn rejects_contact_email_without_explicit_consent() {
        let mut request = valid_request();
        request.contact_email = Some("person@example.com".into());
        assert_eq!(
            request.validate().unwrap_err(),
            FeedbackValidationError::ContactConsentRequired
        );
    }

    #[test]
    fn requires_contact_email_when_contact_is_requested() {
        let mut request = valid_request();
        request.contact_consent = true;
        assert_eq!(
            request.validate().unwrap_err(),
            FeedbackValidationError::ContactEmailRequired
        );
    }

    #[test]
    fn rejects_diagnostics_without_consent() {
        let mut request = valid_request();
        request.diagnostics = Some(FeedbackDiagnostics {
            device_class: DeviceClass::Laptop,
            os_version: Some("test-os".into()),
            locale: None,
        });
        assert_eq!(
            request.validate().unwrap_err(),
            FeedbackValidationError::DiagnosticsConsentRequired
        );
    }

    #[test]
    fn rejects_message_over_limit() {
        let mut request = valid_request();
        request.message = "x".repeat(5_001);
        assert_eq!(
            request.validate().unwrap_err(),
            FeedbackValidationError::MessageTooLong
        );
    }

    #[test]
    fn rejects_message_that_exceeds_byte_limit_with_multibyte_text() {
        let mut request = valid_request();
        request.message = "€".repeat(2_001);
        assert!(request.message.chars().count() <= MAX_MESSAGE_CHARS);
        assert_eq!(
            request.validate().unwrap_err(),
            FeedbackValidationError::MessageTooLong
        );
    }
    #[test]
    fn contact_name_and_feature_are_preserved_only_with_contact_consent() {
        let request = FeedbackSubmissionRequest::new(
            FeedbackCategory::Support,
            None,
            "Please help with account settings.".into(),
            None,
            Some("person@example.test".into()),
            true,
            FeedbackPlatform::Linux,
            "0.5.2".into(),
            false,
            None,
        )
        .with_context(Some(FeedbackFeature::Settings), Some("Anshu".into()));

        let validated = request.validate().expect("consented contact request");
        assert_eq!(validated.affected_feature, Some(FeedbackFeature::Settings));
        assert_eq!(validated.contact_name.as_deref(), Some("Anshu"));
        assert_eq!(
            validated.contact_email.as_deref(),
            Some("person@example.test")
        );
    }

    #[test]
    fn rejects_contact_name_without_contact_consent() {
        let request = valid_request().with_context(None, Some("Anshu".into()));
        assert_eq!(
            request.validate().unwrap_err(),
            FeedbackValidationError::ContactConsentRequired
        );
    }
}
