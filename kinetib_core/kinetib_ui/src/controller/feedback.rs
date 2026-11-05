//! Feedback form mapping plus native/WebAssembly submission controllers.
//!
//! The controller keeps one pending idempotency request after ambiguous transport
//! failures. Retrying sends the identical request; changing or switching accounts
//! requires explicitly discarding that pending request.

use kinetib_ipc_protocol::{
    DeviceClass, FeedbackCategory, FeedbackDiagnostics, FeedbackFeature, FeedbackPlatform,
    FeedbackSubmissionRequest,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum FeedbackFormError {
    InvalidCategory,
    InvalidFeature,
    InvalidRating,
    EmptyMessage,
    MessageTooLong,
    InvalidTitle,
    ContactEmailRequired,
    InvalidContactEmail,
    ContactConsentRequired,
    DiagnosticsConsentRequired,
    InvalidInput,
}

impl FeedbackFormError {
    pub(crate) const fn user_message(self) -> &'static str {
        match self {
            Self::InvalidCategory => "Select a valid feedback category.",
            Self::InvalidFeature => "Select a valid affected feature.",
            Self::InvalidRating => "Select a rating from 1 to 5, or choose no rating.",
            Self::EmptyMessage => "Enter a message describing your feedback.",
            Self::MessageTooLong => "Your message exceeds 5,000 characters or 6,000 UTF-8 bytes.",
            Self::InvalidTitle => "The title must be 120 characters and 120 UTF-8 bytes or fewer.",
            Self::ContactEmailRequired => {
                "Enter a contact email address or turn off contact permission."
            }
            Self::InvalidContactEmail => "Enter a valid contact email address.",
            Self::ContactConsentRequired => {
                "Contact details can only be sent when contact permission is enabled."
            }
            Self::DiagnosticsConsentRequired => {
                "Diagnostics can only be sent when diagnostics permission is enabled."
            }
            Self::InvalidInput => "Some feedback fields are invalid. Review them and try again.",
        }
    }
}

/// Keep Slint's retained feedback inputs bounded while the user types or pastes.
/// The widget may transiently receive a large paste event, but only this bounded
/// prefix is retained in the application model and reused by later operations.
pub(crate) fn attach_feedback_input_limits(window: &crate::MainWindow) {
    use slint::ComponentHandle;

    let weak = window.as_weak();
    window.on_feedback_title_edited({
        let weak = weak.clone();
        move |value| {
            if let Some(ui) = weak.upgrade() {
                let bounded = bounded_input(value.as_str(), 120, 120);
                if bounded.as_str() != value.as_str() {
                    ui.set_feedback_title_input(bounded.into());
                }
            }
        }
    });
    window.on_feedback_message_edited({
        let weak = weak.clone();
        move |value| {
            if let Some(ui) = weak.upgrade() {
                let bounded = bounded_input(value.as_str(), 6_000, 5_000);
                if bounded.as_str() != value.as_str() {
                    ui.set_feedback_message_input(bounded.into());
                }
            }
        }
    });
    window.on_feedback_contact_name_edited({
        let weak = weak.clone();
        move |value| {
            if let Some(ui) = weak.upgrade() {
                let bounded = bounded_input(value.as_str(), 120, 120);
                if bounded.as_str() != value.as_str() {
                    ui.set_feedback_contact_name(bounded.into());
                }
            }
        }
    });
    window.on_feedback_contact_email_edited({
        let weak = weak;
        move |value| {
            if let Some(ui) = weak.upgrade() {
                let bounded = bounded_input(value.as_str(), 254, 254);
                if bounded.as_str() != value.as_str() {
                    ui.set_feedback_contact_email(bounded.into());
                }
            }
        }
    });
}

fn bounded_input(value: &str, max_bytes: usize, max_chars: usize) -> String {
    let mut end = 0;
    let mut chars = 0;
    for (index, character) in value.char_indices() {
        let next = index + character.len_utf8();
        if chars == max_chars || next > max_bytes {
            break;
        }
        end = next;
        chars += 1;
    }
    value[..end].to_owned()
}

#[allow(clippy::too_many_arguments)]
pub(crate) fn build_submission(
    category: &str,
    feature: &str,
    title: &str,
    message: &str,
    rating: &str,
    contact_name: &str,
    contact_email: &str,
    contact_consent: bool,
    diagnostics_consent: bool,
) -> Result<FeedbackSubmissionRequest, FeedbackFormError> {
    let category = parse_category(category).ok_or(FeedbackFormError::InvalidCategory)?;
    let feature = parse_feature(feature)?;
    let rating = parse_rating(rating)?;

    let message = message.trim();
    if message.is_empty() {
        return Err(FeedbackFormError::EmptyMessage);
    }
    if message.chars().count() > 5_000 || message.len() > 6_000 {
        return Err(FeedbackFormError::MessageTooLong);
    }

    let title = title.trim();
    if title.len() > 120 || title.chars().count() > 120 {
        return Err(FeedbackFormError::InvalidTitle);
    }
    let title = optional_trimmed(title);

    let contact_name = if contact_consent {
        let name = contact_name.trim();
        if name.len() > 120 || name.chars().count() > 120 {
            return Err(FeedbackFormError::InvalidInput);
        }
        optional_trimmed(name)
    } else {
        None
    };
    let contact_email = if contact_consent {
        let email = contact_email.trim();
        if email.len() > 254 {
            return Err(FeedbackFormError::InvalidContactEmail);
        }
        optional_trimmed(email)
    } else {
        None
    };
    let diagnostics = diagnostics_consent.then(|| FeedbackDiagnostics {
        device_class: device_class(),
        // Do not collect OS version or locale implicitly. This first UI sends
        // only the coarse device class after the user enables diagnostics.
        os_version: None,
        locale: None,
    });

    let request = FeedbackSubmissionRequest::new(
        category,
        title,
        message.to_owned(),
        rating,
        contact_email,
        contact_consent,
        feedback_platform(),
        env!("CARGO_PKG_VERSION").to_owned(),
        diagnostics_consent,
        diagnostics,
    )
    .with_context(feature, contact_name);

    request.validate().map_err(map_validation_error)?;
    Ok(request)
}

fn parse_category(value: &str) -> Option<FeedbackCategory> {
    Some(match value {
        "bug_report" | "Bug report" => FeedbackCategory::BugReport,
        "feature_request" | "Feature request" => FeedbackCategory::FeatureRequest,
        "general" | "General" => FeedbackCategory::General,
        "support" | "Support" => FeedbackCategory::Support,
        _ => return None,
    })
}

fn parse_feature(value: &str) -> Result<Option<FeedbackFeature>, FeedbackFormError> {
    Ok(match value {
        "" | "unspecified" | "Not specified" => None,
        "dashboard" | "Dashboard" => Some(FeedbackFeature::Dashboard),
        "transactions" | "Transactions" => Some(FeedbackFeature::Transactions),
        "budgets" | "Budgets" => Some(FeedbackFeature::Budgets),
        "emi" | "EMI" => Some(FeedbackFeature::Emi),
        "analytics" | "Analytics" => Some(FeedbackFeature::Analytics),
        "chat" | "Chat" => Some(FeedbackFeature::Chat),
        "vision" | "Vision" => Some(FeedbackFeature::Vision),
        "voice" | "Voice" => Some(FeedbackFeature::Voice),
        "authentication" | "Authentication" => Some(FeedbackFeature::Authentication),
        "settings" | "Settings" => Some(FeedbackFeature::Settings),
        "feedback" | "Feedback" => Some(FeedbackFeature::Feedback),
        "other" | "Other" => Some(FeedbackFeature::Other),
        _ => return Err(FeedbackFormError::InvalidFeature),
    })
}

fn parse_rating(value: &str) -> Result<Option<i16>, FeedbackFormError> {
    if value == "No rating" || value.is_empty() {
        return Ok(None);
    }
    let rating = value
        .parse::<i16>()
        .map_err(|_| FeedbackFormError::InvalidRating)?;
    if (1..=5).contains(&rating) {
        Ok(Some(rating))
    } else {
        Err(FeedbackFormError::InvalidRating)
    }
}

fn optional_trimmed(value: &str) -> Option<String> {
    let value = value.trim();
    (!value.is_empty()).then(|| value.to_owned())
}

fn map_validation_error(
    error: kinetib_ipc_protocol::FeedbackValidationError,
) -> FeedbackFormError {
    use kinetib_ipc_protocol::FeedbackValidationError as Validation;
    match error {
        Validation::EmptyMessage => FeedbackFormError::EmptyMessage,
        Validation::MessageTooLong => FeedbackFormError::MessageTooLong,
        Validation::InvalidTitle => FeedbackFormError::InvalidTitle,
        Validation::ContactEmailRequired => FeedbackFormError::ContactEmailRequired,
        Validation::InvalidContactEmail => FeedbackFormError::InvalidContactEmail,
        Validation::ContactConsentRequired => FeedbackFormError::ContactConsentRequired,
        Validation::DiagnosticsConsentRequired => FeedbackFormError::DiagnosticsConsentRequired,
        Validation::InvalidRating => FeedbackFormError::InvalidRating,
        _ => FeedbackFormError::InvalidInput,
    }
}

fn feedback_platform() -> FeedbackPlatform {
    #[cfg(target_arch = "wasm32")]
    {
        FeedbackPlatform::Web
    }
    #[cfg(target_os = "android")]
    {
        FeedbackPlatform::Android
    }
    #[cfg(target_os = "ios")]
    {
        FeedbackPlatform::Ios
    }
    #[cfg(target_os = "windows")]
    {
        FeedbackPlatform::Windows
    }
    #[cfg(target_os = "macos")]
    {
        FeedbackPlatform::Macos
    }
    #[cfg(target_os = "linux")]
    {
        FeedbackPlatform::Linux
    }
    #[cfg(not(any(
        target_arch = "wasm32",
        target_os = "android",
        target_os = "ios",
        target_os = "windows",
        target_os = "macos",
        target_os = "linux"
    )))]
    {
        FeedbackPlatform::Other
    }
}

fn device_class() -> DeviceClass {
    #[cfg(target_arch = "wasm32")]
    {
        DeviceClass::Browser
    }
    #[cfg(not(target_arch = "wasm32"))]
    {
        DeviceClass::Unknown
    }
}

fn should_retain_for_retry(error: crate::backend::BackendError) -> bool {
    use crate::backend::BackendError;
    matches!(
        error,
        BackendError::TimedOut
            | BackendError::Unavailable
            | BackendError::Busy
            | BackendError::OutcomeUnknown
            | BackendError::IncompatibleProtocol
            | BackendError::SessionRequired
            | BackendError::SessionExpired
    )
}

fn requires_reauthentication(error: crate::backend::BackendError) -> bool {
    use crate::backend::BackendError;
    matches!(
        error,
        BackendError::TimedOut
            | BackendError::Unavailable
            | BackendError::IncompatibleProtocol
            | BackendError::SessionRequired
            | BackendError::SessionExpired
    )
}

#[derive(Clone)]
pub(crate) struct PendingFeedback {
    request: FeedbackSubmissionRequest,
    account_email: String,
}

fn normalize_account_email(email: &str) -> String {
    // Match the exact login identifier; fail closed rather than treating two
    // differently-cased identifiers as the same account during a retry.
    email.trim().to_owned()
}

pub(crate) fn reset_feedback_ui(ui: &crate::MainWindow) {
    clear_form(ui);
    ui.set_feedback_status("Share an idea, report a bug, or ask for support.".into());
}

fn clear_form(ui: &crate::MainWindow) {
    ui.set_feedback_category("General".into());
    ui.set_feedback_affected_feature("Not specified".into());
    ui.set_feedback_title_input("".into());
    ui.set_feedback_message_input("".into());
    ui.set_feedback_rating("No rating".into());
    ui.set_feedback_contact_name("".into());
    ui.set_feedback_contact_email("".into());
    ui.set_feedback_contact_consent(false);
    ui.set_feedback_diagnostics_consent(false);
    ui.set_feedback_retry_pending(false);
}

#[cfg(not(target_arch = "wasm32"))]
pub(crate) type NativePending = std::sync::Arc<std::sync::Mutex<Option<PendingFeedback>>>;

#[cfg(not(target_arch = "wasm32"))]
pub(crate) fn clear_native_pending(pending: &NativePending) {
    if let Ok(mut slot) = pending.lock() {
        slot.take();
    }
}

#[cfg(not(target_arch = "wasm32"))]
pub(crate) fn native_pending_matches_account(pending: &NativePending, email: &str) -> bool {
    let account = normalize_account_email(email);
    if account.is_empty() {
        return false;
    }
    pending
        .lock()
        .ok()
        .and_then(|slot| {
            slot.as_ref()
                .map(|item| item.account_email.as_str() == account.as_str())
        })
        .unwrap_or(false)
}

#[cfg(not(target_arch = "wasm32"))]
pub(crate) fn attach_native_feedback(
    window: &crate::MainWindow,
    backend: std::sync::Arc<crate::backend::UiBackend>,
    runtime: tokio::runtime::Handle,
) -> NativePending {
    use slint::ComponentHandle;

    attach_feedback_input_limits(window);
    let pending: NativePending = std::sync::Arc::new(std::sync::Mutex::new(None));
    let weak = window.as_weak();

    window.on_feedback_submit_requested({
        let backend = backend.clone();
        let pending = pending.clone();
        let weak = weak.clone();
        let runtime = runtime.clone();
        move |category, feature, title, message, rating, contact_name, contact_email,
              contact_consent, diagnostics_consent| {
            let Some(ui) = weak.upgrade() else { return; };
            if ui.get_local_only_mode() {
                ui.set_feedback_status("Feedback submission requires a CloudSync session.".into());
                return;
            }

            let request = {
                let Ok(mut slot) = pending.lock() else {
                    ui.set_feedback_status("Feedback is temporarily unavailable. Try again.".into());
                    return;
                };
                let account_email = normalize_account_email(ui.get_email().as_str());
                if account_email.is_empty() {
                    ui.set_feedback_status(
                        "Sign in with the account email before submitting feedback.".into(),
                    );
                    return;
                }
                if let Some(existing) = slot
                    .as_ref()
                    .filter(|item| item.account_email.as_str() == account_email.as_str())
                {
                    existing.request.clone()
                } else {
                    slot.take();
                    match build_submission(
                        category.as_str(), feature.as_str(), title.as_str(), message.as_str(),
                        rating.as_str(), contact_name.as_str(), contact_email.as_str(),
                        contact_consent, diagnostics_consent,
                    ) {
                        Ok(request) => {
                            *slot = Some(PendingFeedback {
                                request: request.clone(),
                                account_email,
                            });
                            request
                        }
                        Err(error) => {
                            ui.set_feedback_status(error.user_message().into());
                            return;
                        }
                    }
                }
            };

            ui.set_busy(true);
            ui.set_feedback_status("Submitting feedback securely…".into());

            let backend = backend.clone();
            let pending = pending.clone();
            let weak = weak.clone();
            runtime.spawn(async move {
                let result = backend.submit_feedback(request).await;
                let retain = matches!(&result, Err(error) if should_retain_for_retry(*error));
                if !retain {
                    clear_native_pending(&pending);
                }

                let _ = slint::invoke_from_event_loop(move || {
                    if let Some(ui) = weak.upgrade() {
                        match result {
                            Ok(receipt) => {
                                let state = if receipt.duplicate {
                                    "This feedback was already accepted."
                                } else {
                                    "Thank you. Your feedback was submitted."
                                };
                                ui.set_feedback_status(
                                    format!("{state} Reference: {}", receipt.submission_id).into(),
                                );
                                clear_form(&ui);
                            }
                            Err(error) => {
                                ui.set_feedback_status(
                                    if retain {
                                        format!(
                                            "{} Your original submission is retained; retry it unchanged.",
                                            error.user_message()
                                        )
                                        .into()
                                    } else {
                                        error.user_message().into()
                                    },
                                );
                                ui.set_feedback_retry_pending(retain);
                                if requires_reauthentication(error) {
                                    ui.set_authenticated(false);
                                }
                            }
                        }
                        ui.set_busy(false);
                    }
                });
            });
        }
    });

    window.on_feedback_retry_discard_requested({
        let pending = pending.clone();
        let weak = weak.clone();
        move || {
            clear_native_pending(&pending);
            if let Some(ui) = weak.upgrade() {
                ui.set_feedback_retry_pending(false);
                ui.set_feedback_status(
                    "Pending retry discarded. You can edit and submit a new report.".into(),
                );
            }
        }
    });

    pending
}

#[cfg(target_arch = "wasm32")]
pub(crate) type BrowserPending = std::rc::Rc<std::cell::RefCell<Option<PendingFeedback>>>;

#[cfg(target_arch = "wasm32")]
pub(crate) fn clear_browser_pending(pending: &BrowserPending) {
    pending.borrow_mut().take();
}

#[cfg(target_arch = "wasm32")]
pub(crate) fn browser_pending_matches_account(pending: &BrowserPending, email: &str) -> bool {
    let account = normalize_account_email(email);
    !account.is_empty()
        && pending
            .borrow()
            .as_ref()
            .map(|item| item.account_email.as_str() == account.as_str())
            .unwrap_or(false)
}

#[cfg(target_arch = "wasm32")]
pub(crate) fn attach_browser_feedback(
    window: &crate::MainWindow,
    backend: std::rc::Rc<crate::backend::UiBackend>,
) -> BrowserPending {
    use slint::ComponentHandle;

    attach_feedback_input_limits(window);
    let pending: BrowserPending = std::rc::Rc::new(std::cell::RefCell::new(None));
    let weak = window.as_weak();

    window.on_feedback_submit_requested({
        let backend = backend.clone();
        let pending = pending.clone();
        let weak = weak.clone();
        move |category,
              feature,
              title,
              message,
              rating,
              contact_name,
              contact_email,
              contact_consent,
              diagnostics_consent| {
            let Some(ui) = weak.upgrade() else {
                return;
            };
            if ui.get_local_only_mode() {
                ui.set_feedback_status("Feedback submission requires a CloudSync session.".into());
                return;
            }

            let request = {
                let mut slot = pending.borrow_mut();
                let account_email = normalize_account_email(ui.get_email().as_str());
                if account_email.is_empty() {
                    ui.set_feedback_status(
                        "Sign in with the account email before submitting feedback.".into(),
                    );
                    return;
                }
                if let Some(existing) = slot
                    .as_ref()
                    .filter(|item| item.account_email.as_str() == account_email.as_str())
                {
                    existing.request.clone()
                } else {
                    slot.take();
                    match build_submission(
                        category.as_str(),
                        feature.as_str(),
                        title.as_str(),
                        message.as_str(),
                        rating.as_str(),
                        contact_name.as_str(),
                        contact_email.as_str(),
                        contact_consent,
                        diagnostics_consent,
                    ) {
                        Ok(request) => {
                            *slot = Some(PendingFeedback {
                                request: request.clone(),
                                account_email,
                            });
                            request
                        }
                        Err(error) => {
                            ui.set_feedback_status(error.user_message().into());
                            return;
                        }
                    }
                }
            };

            ui.set_busy(true);
            ui.set_feedback_status("Submitting feedback securely…".into());
            let backend = backend.clone();
            let pending = pending.clone();
            let weak = weak.clone();
            slint::spawn_local(async move {
                let result = backend.submit_feedback(request).await;
                let retain = matches!(&result, Err(error) if should_retain_for_retry(*error));
                if !retain {
                    clear_browser_pending(&pending);
                }

                if let Some(ui) = weak.upgrade() {
                    match result {
                        Ok(receipt) => {
                            let state = if receipt.duplicate {
                                "This feedback was already accepted."
                            } else {
                                "Thank you. Your feedback was submitted."
                            };
                            ui.set_feedback_status(
                                format!("{state} Reference: {}", receipt.submission_id).into(),
                            );
                            clear_form(&ui);
                        }
                        Err(error) => {
                            ui.set_feedback_status(if retain {
                                format!(
                                    "{} Your original submission is retained; retry it unchanged.",
                                    error.user_message()
                                )
                                .into()
                            } else {
                                error.user_message().into()
                            });
                            ui.set_feedback_retry_pending(retain);
                            if requires_reauthentication(error) {
                                ui.set_authenticated(false);
                            }
                        }
                    }
                    ui.set_busy(false);
                }
            });
        }
    });

    window.on_feedback_retry_discard_requested({
        let pending = pending.clone();
        let weak = weak.clone();
        move || {
            clear_browser_pending(&pending);
            if let Some(ui) = weak.upgrade() {
                ui.set_feedback_retry_pending(false);
                ui.set_feedback_status(
                    "Pending retry discarded. You can edit and submit a new report.".into(),
                );
            }
        }
    });

    pending
}

#[cfg(test)]
mod tests {
    use super::*;

    fn build(
        contact_consent: bool,
        email: &str,
        diagnostics_consent: bool,
    ) -> Result<FeedbackSubmissionRequest, FeedbackFormError> {
        build_submission(
            "bug_report",
            "transactions",
            "Title",
            "Description of the issue",
            "4",
            "Anshu",
            email,
            contact_consent,
            diagnostics_consent,
        )
    }

    #[test]
    fn valid_submission_maps_ui_values_to_shared_domain_types() {
        let request = build(false, "ignored@example.test", false).unwrap();
        assert_eq!(request.category, FeedbackCategory::BugReport);
        assert_eq!(
            request.affected_feature,
            Some(FeedbackFeature::Transactions)
        );
        assert_eq!(request.experience_rating, Some(4));
        assert!(request.contact_email.is_none());
        assert!(request.contact_name.is_none());
        assert!(request.diagnostics.is_none());
    }

    #[test]
    fn contact_information_is_only_included_with_explicit_consent() {
        let request = build(true, "person@example.test", false).unwrap();
        assert_eq!(
            request.contact_email.as_deref(),
            Some("person@example.test")
        );
        assert_eq!(request.contact_name.as_deref(), Some("Anshu"));
        assert!(request.contact_consent);
    }

    #[test]
    fn contact_consent_requires_an_email_address() {
        assert_eq!(
            build(true, "", false).unwrap_err(),
            FeedbackFormError::ContactEmailRequired
        );
    }

    #[test]
    fn diagnostics_are_omitted_without_consent_and_minimal_with_consent() {
        let without = build(false, "", false).unwrap();
        assert!(without.diagnostics.is_none());
        let with = build(false, "", true).unwrap();
        let diagnostics = with
            .diagnostics
            .expect("explicit consent creates diagnostics");
        assert_eq!(diagnostics.os_version, None);
        assert_eq!(diagnostics.locale, None);
    }

    #[test]
    fn overlong_message_is_rejected_before_transport() {
        let error = build_submission(
            "general",
            "unspecified",
            "",
            &"x".repeat(5_001),
            "No rating",
            "",
            "",
            false,
            false,
        )
        .unwrap_err();
        assert_eq!(error, FeedbackFormError::MessageTooLong);
    }

    #[test]
    fn maximum_feedback_request_fits_the_ipc_request_budget() {
        let request = build_submission(
            "general",
            "voice",
            &"T".repeat(120),
            &"x".repeat(5_000),
            "5",
            "",
            "",
            false,
            true,
        )
        .expect("maximum valid feedback request");
        let payload =
            kinetib_ipc_protocol::encode_payload(&request).expect("feedback request serializes");
        assert!(
            payload.len() <= kinetib_ipc_protocol::MAX_REQUEST_PAYLOAD_BYTES,
            "encoded feedback payload was {} bytes",
            payload.len()
        );
    }

    #[test]
    fn bounded_input_respects_utf8_byte_and_character_limits() {
        assert_eq!(bounded_input("abcdef", 4, 3), "abc");
        assert_eq!(bounded_input("कखगघ", 6, 10), "कख");
        assert_eq!(bounded_input("a😀b", 5, 2), "a😀");
    }

    #[test]
    fn unsupported_category_feature_and_rating_are_rejected() {
        assert_eq!(
            build_submission(
                "untrusted",
                "",
                "",
                "Message",
                "No rating",
                "",
                "",
                false,
                false
            )
            .unwrap_err(),
            FeedbackFormError::InvalidCategory
        );
        assert_eq!(
            build_submission(
                "general",
                "unknown",
                "",
                "Message",
                "No rating",
                "",
                "",
                false,
                false,
            )
            .unwrap_err(),
            FeedbackFormError::InvalidFeature
        );
        assert_eq!(
            build_submission("general", "", "", "Message", "6", "", "", false, false).unwrap_err(),
            FeedbackFormError::InvalidRating
        );
    }
}
