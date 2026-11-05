//! Bounded event protocol for FinTally Voice.
//!
//! Responsibilities:
//! - Represent events exchanged between audio, DSP, ASR, review, and UI workers.
//! - Keep event payloads fixed-size and allocation-free.
//! - Preserve raw ASR text separately from cleaned or edited prompt text.
//! - Carry sequence numbers for ordering and stale-event detection.
//!
//! Non-responsibilities:
//! - Thread creation and CPU affinity (`affinity.rs`).
//! - Audio capture or DSP computation.
//! - ASR inference or intent classification.
//! - Sending prompts to an LLM.
//!
//! Design notes:
//! - `repr(C, align(64))` is used only where a stable, cache-aligned layout is
//!   useful. It does not guarantee that every instance starts at a 64-byte
//!   boundary when allocated inside another object.
//! - Keep frequently exchanged events compact. Cache alignment is not free:
//!   padding every small event to 64 bytes can increase queue bandwidth.
//! - Do not treat VQ codebook indices as words or transcripts.

use core::fmt;
use core::str;

/// Fixed UTF-8 text capacity for event payloads.
///
/// This is a byte limit, not a Unicode-character limit. A truncated UTF-8
/// string must never be exposed as valid text.
pub const EVENT_TEXT_CAPACITY: usize = 192;

/// Fixed capacity for a short diagnostic/error message.
pub const EVENT_ERROR_CAPACITY: usize = 96;

/// Maximum accepted event payload bytes. Keep this independent of audio
/// buffers and ASR model memory.
pub const MAX_EVENT_TEXT_BYTES: usize = EVENT_TEXT_CAPACITY;

/// Event protocol version. Increment when changing the externally consumed
/// event representation incompatibly.
pub const EVENT_PROTOCOL_VERSION: u16 = 1;

/// Event text stored inline without a heap allocation.
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct FixedText<const N: usize> {
    bytes: [u8; N],
    len: u16,
}

impl<const N: usize> FixedText<N> {
    /// Construct an empty text buffer.
    pub const fn new() -> Self {
        Self {
            bytes: [0; N],
            len: 0,
        }
    }

    /// Construct from UTF-8 text, failing if it exceeds the fixed capacity.
    ///
    /// No truncation is performed because silently truncating text can change
    /// a financial command or an LLM prompt.
    pub fn try_from_str(value: &str) -> Result<Self, TextCapacityError> {
        if value.len() > N {
            return Err(TextCapacityError {
                actual_bytes: value.len(),
                capacity_bytes: N,
            });
        }

        if N > u16::MAX as usize {
            return Err(TextCapacityError {
                actual_bytes: value.len(),
                capacity_bytes: u16::MAX as usize,
            });
        }

        let mut result = Self::new();
        result.bytes[..value.len()].copy_from_slice(value.as_bytes());
        result.len = value.len() as u16;
        Ok(result)
    }

    /// Return the text as UTF-8.
    ///
    /// The invariant is maintained by `try_from_str` and the private fields.
    pub fn as_str(&self) -> &str {
        // SAFETY: `bytes[..len]` is copied only from a valid `str`; the fields
        // are private, and `len` is set only by `try_from_str`.
        unsafe { str::from_utf8_unchecked(&self.bytes[..self.len()]) }
    }

    /// Return the number of UTF-8 bytes stored.
    pub const fn len(&self) -> usize {
        self.len as usize
    }

    /// Whether the text buffer contains no bytes.
    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Maximum number of bytes this buffer can store.
    pub const fn capacity(&self) -> usize {
        N
    }

    /// Return the initialized byte prefix.
    pub fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.len()]
    }
}

impl<const N: usize> Default for FixedText<N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const N: usize> fmt::Debug for FixedText<N> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("FixedText")
            .field("text", &self.as_str())
            .field("len", &self.len())
            .field("capacity", &N)
            .finish()
    }
}

impl<const N: usize> fmt::Display for FixedText<N> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TextCapacityError {
    pub actual_bytes: usize,
    pub capacity_bytes: usize,
}

impl fmt::Display for TextCapacityError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "text is {} bytes; fixed capacity is {} bytes",
            self.actual_bytes, self.capacity_bytes
        )
    }
}

impl std::error::Error for TextCapacityError {}

/// Fixed-size transcript/prompt text.
pub type EventText = FixedText<EVENT_TEXT_CAPACITY>;

/// Fixed-size diagnostic text.
pub type EventErrorText = FixedText<EVENT_ERROR_CAPACITY>;

/// Identifies the worker or subsystem that emitted an event.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum EventSource {
    Audio = 0,
    Dsp = 1,
    Asr = 2,
    Transcript = 3,
    Intent = 4,
    PromptReview = 5,
    Ui = 6,
    LlmController = 7,
    Runtime = 8,
}

/// High-level event kinds used for routing and diagnostics.
///
/// Prefer matching on the typed `VoiceEventKind` rather than relying on
/// numeric values in application logic.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum VoiceEventKind {
    AudioStarted = 0,
    AudioStopped = 1,
    AudioOverrun = 2,
    DspFrameReady = 3,
    AsrPartial = 4,
    AsrFinal = 5,
    TranscriptCleaned = 6,
    IntentReady = 7,
    PromptDraftReady = 8,
    PromptApproved = 9,
    PromptRejected = 10,
    ProcessingCancelled = 11,
    WorkerFault = 12,
    RuntimeShutdown = 13,
}

/// A compact, typed event for worker-to-worker notification.
///
/// Text is inline and fixed-size. This struct is intentionally not declared
/// `repr(C)`: use the dedicated C FFI shim and its ABI-defined structures for
/// cross-language messages. Rust enum layout is not a stable C ABI.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VoiceEvent {
    pub protocol_version: u16,
    pub kind: VoiceEventKind,
    pub source: EventSource,
    pub sequence: u64,
    pub session_id: u32,
    pub flags: u16,
    pub text: EventText,
}

impl VoiceEvent {
    /// Construct an event with an empty text payload.
    pub const fn new(
        kind: VoiceEventKind,
        source: EventSource,
        sequence: u64,
        session_id: u32,
    ) -> Self {
        Self {
            protocol_version: EVENT_PROTOCOL_VERSION,
            kind,
            source,
            sequence,
            session_id,
            flags: 0,
            text: EventText::new(),
        }
    }

    /// Construct an event containing bounded UTF-8 text.
    pub fn with_text(
        kind: VoiceEventKind,
        source: EventSource,
        sequence: u64,
        session_id: u32,
        text: &str,
    ) -> Result<Self, TextCapacityError> {
        Ok(Self {
            text: EventText::try_from_str(text)?,
            ..Self::new(kind, source, sequence, session_id)
        })
    }

    /// Set event flags.
    ///
    /// Define flag meanings in the protocol layer that consumes them; do not
    /// reuse a flag with a different meaning in different workers.
    pub const fn with_flags(mut self, flags: u16) -> Self {
        self.flags = flags;
        self
    }

    /// Check that this event uses the current protocol version.
    pub const fn has_current_protocol(&self) -> bool {
        self.protocol_version == EVENT_PROTOCOL_VERSION
    }
}

/// Cache-line-aligned metadata for a single-producer/single-consumer worker.
///
/// Use this for queue indices or counters only when the actual queue
/// implementation and measurements justify it. Do not wrap every event in
/// this type: doing so can waste cache and memory bandwidth.
///
/// For a real SPSC queue, producer-owned and consumer-owned indices should
/// generally occupy separate cache lines to reduce false sharing.
#[repr(C, align(64))]
#[derive(Debug, Clone, Copy, Default)]
pub struct CacheLineU64 {
    pub value: u64,
}

/// A compact, non-sensitive diagnostic classification.
///
/// Avoid putting full financial records, access tokens, or complete prompts in
/// diagnostic events.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
pub enum RuntimeFaultCode {
    Unknown = 0,
    InvalidAudioFormat = 1,
    AudioBufferOverrun = 2,
    AsrUnavailable = 3,
    AsrFailed = 4,
    TranscriptTooLong = 5,
    IntentFailed = 6,
    PromptDraftTooLong = 7,
    StaleApproval = 8,
    QueueClosed = 9,
    WorkerPanicked = 10,
}

/// Fault details that can be passed without heap-allocated strings.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RuntimeFault {
    pub code: RuntimeFaultCode,
    pub source: EventSource,
    pub sequence: u64,
    pub session_id: u32,
    pub message: EventErrorText,
}

impl RuntimeFault {
    pub fn new(
        code: RuntimeFaultCode,
        source: EventSource,
        sequence: u64,
        session_id: u32,
        message: &str,
    ) -> Result<Self, TextCapacityError> {
        Ok(Self {
            code,
            source,
            sequence,
            session_id,
            message: EventErrorText::try_from_str(message)?,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fixed_text_round_trips_utf8_without_heap_allocation() {
        let text = EventText::try_from_str("hello 🦀").unwrap();

        assert_eq!(text.as_str(), "hello 🦀");
        assert_eq!(text.len(), "hello 🦀".len());
        assert!(!text.is_empty());
    }

    #[test]
    fn fixed_text_rejects_oversized_input_instead_of_truncating() {
        let input = "x".repeat(EVENT_TEXT_CAPACITY + 1);
        let error = EventText::try_from_str(&input).unwrap_err();

        assert_eq!(error.actual_bytes, EVENT_TEXT_CAPACITY + 1);
        assert_eq!(error.capacity_bytes, EVENT_TEXT_CAPACITY);
    }

    #[test]
    fn empty_text_is_valid() {
        let text = EventText::new();

        assert!(text.is_empty());
        assert_eq!(text.as_str(), "");
        assert_eq!(text.capacity(), EVENT_TEXT_CAPACITY);
    }

    #[test]
    fn event_keeps_protocol_and_sequence_metadata() {
        let event = VoiceEvent::with_text(
            VoiceEventKind::AsrFinal,
            EventSource::Asr,
            42,
            7,
            "Pay my electricity bill",
        )
        .unwrap();

        assert!(event.has_current_protocol());
        assert_eq!(event.sequence, 42);
        assert_eq!(event.session_id, 7);
        assert_eq!(event.text.as_str(), "Pay my electricity bill");
    }

    #[test]
    fn fixed_text_rejects_invalid_capacity_type() {
        // A zero-capacity buffer is valid as a type, but cannot store text.
        let error = FixedText::<0>::try_from_str("a").unwrap_err();

        assert_eq!(error.actual_bytes, 1);
        assert_eq!(error.capacity_bytes, 0);
    }

    #[test]
    fn cache_line_metadata_has_expected_alignment() {
        assert_eq!(core::mem::align_of::<CacheLineU64>(), 64);
        assert_eq!(core::mem::size_of::<CacheLineU64>(), 64);
    }
}
