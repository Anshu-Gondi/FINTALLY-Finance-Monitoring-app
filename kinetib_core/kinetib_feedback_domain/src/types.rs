use serde::{Deserialize, Serialize};
use uuid::Uuid;

/// User-selected feedback intent.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FeedbackCategory {
    BugReport,
    FeatureRequest,
    General,
    Support,
}

impl FeedbackCategory {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::BugReport => "bug_report",
            Self::FeatureRequest => "feature_request",
            Self::General => "general",
            Self::Support => "support",
        }
    }
}

/// Allowlisted FinTally feature area affected by a report.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FeedbackFeature {
    Dashboard,
    Transactions,
    Budgets,
    Emi,
    Analytics,
    Chat,
    Vision,
    Voice,
    Authentication,
    Settings,
    Feedback,
    Other,
}

impl FeedbackFeature {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Dashboard => "dashboard",
            Self::Transactions => "transactions",
            Self::Budgets => "budgets",
            Self::Emi => "emi",
            Self::Analytics => "analytics",
            Self::Chat => "chat",
            Self::Vision => "vision",
            Self::Voice => "voice",
            Self::Authentication => "authentication",
            Self::Settings => "settings",
            Self::Feedback => "feedback",
            Self::Other => "other",
        }
    }
}

/// Runtime platform reported by the client. This is descriptive metadata,
/// never an authentication or authorization signal.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FeedbackPlatform {
    Linux,
    Windows,
    Macos,
    Ios,
    Android,
    Web,
    Embedded,
    Other,
}

impl FeedbackPlatform {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Linux => "linux",
            Self::Windows => "windows",
            Self::Macos => "macos",
            Self::Ios => "ios",
            Self::Android => "android",
            Self::Web => "web",
            Self::Embedded => "embedded",
            Self::Other => "other",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DeviceClass {
    Desktop,
    Laptop,
    Tablet,
    Phone,
    Watch,
    Embedded,
    Browser,
    Unknown,
}

impl DeviceClass {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Desktop => "desktop",
            Self::Laptop => "laptop",
            Self::Tablet => "tablet",
            Self::Phone => "phone",
            Self::Watch => "watch",
            Self::Embedded => "embedded",
            Self::Browser => "browser",
            Self::Unknown => "unknown",
        }
    }
}

/// Deliberately excludes hardware identifiers, advertising IDs, file paths,
/// transaction details, screenshots and raw diagnostic logs.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FeedbackDiagnostics {
    pub device_class: DeviceClass,
    pub os_version: Option<String>,
    pub locale: Option<String>,
}

/// Acknowledgement for an accepted submission. `duplicate` is true when the
/// server recognized an identical retry using the same submission UUID.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FeedbackReceipt {
    pub submission_id: Uuid,
    pub duplicate: bool,
}
