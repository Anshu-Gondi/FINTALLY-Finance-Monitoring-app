//! Public configuration, errors, and value types for the voice engine.

use crate::ffi::{self, AbiCheckError};
use std::{error::Error, fmt};

pub(super) const MAX_DSP_WORKING_SET_BYTES: usize = 100 * 1024;
pub(super) const MAX_EVENT_CAPACITY: usize = 256;
pub(super) const FEATURE_DIMENSION: usize = 32;
const DEFAULT_EVENT_CAPACITY: usize = 32;
const DEFAULT_TEXT_CAPACITY_BYTES: usize = 4 * 1024;
const MIN_TEXT_CAPACITY_BYTES: usize = 96;
const MAX_TEXT_CAPACITY_BYTES: usize = 8 * 1024;

/// Configuration for bounded native buffers owned by the engine.
///
/// The audio format is intentionally fixed to the C core's validated contract:
/// mono PCM16 at 16 kHz, with 320-sample frames and 160-sample hops.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct EngineConfig {
    /// Number of fixed-size events in the native SPSC queue.
    pub event_capacity: usize,
    /// Capacity of the C text builder, in bytes (not Unicode characters).
    pub text_capacity_bytes: usize,
    /// Maximum accounted DSP working set, including one active codebook.
    /// This cannot exceed 100 KiB. ASR model memory is outside this budget.
    pub working_set_budget_bytes: usize,
}

impl Default for EngineConfig {
    fn default() -> Self {
        Self {
            event_capacity: DEFAULT_EVENT_CAPACITY,
            text_capacity_bytes: DEFAULT_TEXT_CAPACITY_BYTES,
            working_set_budget_bytes: MAX_DSP_WORKING_SET_BYTES,
        }
    }
}

impl EngineConfig {
    pub(super) fn validate(self) -> Result<Self, EngineError> {
        if !(2..=MAX_EVENT_CAPACITY).contains(&self.event_capacity) {
            return Err(EngineError::InvalidConfiguration(
                "event_capacity must be between 2 and 256",
            ));
        }
        if !(MIN_TEXT_CAPACITY_BYTES..=MAX_TEXT_CAPACITY_BYTES).contains(&self.text_capacity_bytes)
        {
            return Err(EngineError::InvalidConfiguration(
                "text_capacity_bytes must be between 96 and 8192",
            ));
        }
        if self.working_set_budget_bytes == 0
            || self.working_set_budget_bytes > MAX_DSP_WORKING_SET_BYTES
        {
            return Err(EngineError::InvalidConfiguration(
                "working_set_budget_bytes must be in 1..=102400",
            ));
        }
        Ok(self)
    }
}

/// Runtime lifecycle. `Stopped` is terminal; drop the engine to release memory.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EngineState {
    /// Native components have been initialized, but audio processing is paused.
    Initialized,
    /// The owner thread may process audio frames.
    Running,
    /// The engine has been shut down and cannot be restarted.
    Stopped,
}

/// Language explicitly selected by configuration/user or confirmed by evidence.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VoiceLanguage {
    Unknown,
    English,
    French,
    Hindi,
}

impl VoiceLanguage {
    pub(super) fn from_native(value: ffi::FinVoiceLanguage) -> Result<Self, EngineError> {
        match value {
            ffi::FinVoiceLanguage_FIN_VOICE_LANGUAGE_UNKNOWN => Ok(Self::Unknown),
            ffi::FinVoiceLanguage_FIN_VOICE_LANGUAGE_EN => Ok(Self::English),
            ffi::FinVoiceLanguage_FIN_VOICE_LANGUAGE_FR => Ok(Self::French),
            ffi::FinVoiceLanguage_FIN_VOICE_LANGUAGE_HI => Ok(Self::Hindi),
            _ => Err(EngineError::InvalidNativeResult(
                "native core returned an unsupported language identifier",
            )),
        }
    }
}

/// Accounted memory used by the DSP/pipeline path (not ASR weights or allocator metadata).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct EngineMemoryReport {
    pub dsp_workspace_bytes: usize,
    pub pipeline_workspace_bytes: usize,
    pub event_storage_bytes: usize,
    pub text_storage_bytes: usize,
    pub native_context_bytes: usize,
    pub active_codebook_bytes: usize,
    pub one_frame_transient_bytes: usize,
    pub alignment_slack_bytes: usize,
    pub total_accounted_bytes: usize,
    pub budget_bytes: usize,
}

/// Counters are updated without allocation on the engine-owner thread.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct EngineStats {
    pub frames_processed: u64,
    pub frames_published: u64,
    pub event_publish_failures: u64,
    pub events_popped: u64,
    pub native_events_dropped: usize,
}

/// Compact result of processing one fixed-size PCM16 frame.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct FrameSummary {
    pub language: VoiceLanguage,
    pub best_centroid: Option<usize>,
    pub second_best_centroid: Option<usize>,
    pub best_distance_squared: f32,
    pub second_best_distance_squared: f32,
    pub relative_gap: f32,
}

/// Text payload copied into fixed-size Rust storage; no allocation is performed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FixedText {
    pub(super) bytes: [u8; ffi::FIN_VOICE_EVENT_TEXT_CAPACITY as usize],
    pub(super) len: usize,
}

impl FixedText {
    pub fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.len]
    }

    /// Native text events are validated as UTF-8 before `EngineEvent` is returned.
    pub fn as_str(&self) -> &str {
        // SAFETY: `try_pop_event` validates UTF-8 before constructing `FixedText`.
        unsafe { core::str::from_utf8_unchecked(self.as_bytes()) }
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
}

/// Decoded native event. Variants use fixed-size values to avoid heap allocation.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum EngineEventKind {
    SpeechStarted,
    SpeechEnded,
    FeaturesReady {
        dimension: u16,
        values: [f32; FEATURE_DIMENSION],
    },
    QuantizationResult {
        best_index: Option<usize>,
        second_best_index: Option<usize>,
        best_distance_squared: f32,
        second_best_distance_squared: f32,
        relative_gap: f32,
        language_id: u8,
    },
    TextReady {
        text: FixedText,
        is_final: bool,
    },
    Error {
        code: u32,
        detail: i32,
    },
    Status {
        code: u32,
        detail: i32,
    },
    Shutdown,
    Reset,
    Unknown {
        raw_type: u16,
    },
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct EngineEvent {
    pub timestamp_ns: u64,
    pub sample_position: u64,
    pub sequence_id: u32,
    pub flags: u16,
    pub kind: EngineEventKind,
}

/// Errors from configuration, the ABI boundary, or a native component.
#[derive(Debug)]
pub enum EngineError {
    Abi(AbiCheckError),
    InvalidConfiguration(&'static str),
    InvalidState {
        operation: &'static str,
        state: EngineState,
    },
    NativeComponentFailed {
        component: &'static str,
        status: u32,
    },
    AllocationFailed {
        component: &'static str,
        bytes: usize,
    },
    SizeOverflow(&'static str),
    WorkingSetExceeded {
        required_bytes: usize,
        budget_bytes: usize,
    },
    InvalidLanguageTag,
    InvalidFrameLength {
        expected: usize,
        actual: usize,
    },
    InvalidNativeResult(&'static str),
    InvalidNativeEvent(&'static str),
}

impl fmt::Display for EngineError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Abi(error) => write!(f, "native ABI validation failed: {error}"),
            Self::InvalidConfiguration(message) => {
                write!(f, "invalid engine configuration: {message}")
            }
            Self::InvalidState { operation, state } => {
                write!(f, "cannot {operation} while engine is {state:?}")
            }
            Self::NativeComponentFailed { component, status } => {
                write!(
                    f,
                    "native component {component} failed with status {status}"
                )
            }
            Self::AllocationFailed { component, bytes } => {
                write!(f, "could not allocate {bytes} bytes for {component}")
            }
            Self::SizeOverflow(operation) => {
                write!(f, "size overflow while calculating {operation}")
            }
            Self::WorkingSetExceeded {
                required_bytes,
                budget_bytes,
            } => write!(
                f,
                "DSP working set requires {required_bytes} bytes, exceeding budget {budget_bytes}"
            ),
            Self::InvalidLanguageTag => write!(f, "language tag contains an interior NUL byte"),
            Self::InvalidFrameLength { expected, actual } => {
                write!(f, "expected {expected} PCM16 samples, received {actual}")
            }
            Self::InvalidNativeResult(message) => write!(f, "invalid native result: {message}"),
            Self::InvalidNativeEvent(message) => write!(f, "invalid native event: {message}"),
        }
    }
}

impl Error for EngineError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            Self::Abi(error) => Some(error),
            _ => None,
        }
    }
}
