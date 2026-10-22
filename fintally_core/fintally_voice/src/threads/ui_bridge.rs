//! Bounded bidirectional bridge between the runtime/service layer and a UI.
//!
//! These are Rust-only messages, not C ABI structures. Keep this bridge on the
//! control plane: it is not a realtime audio queue and should not be used to
//! transport every DSP feature vector. Commands/events use bounded `sync_channel`
//! queues; every full/closed send returns the original message to the caller.
//! UI text is length-capped and Debug never prints its contents.
//!
//! Cache-line padding is intentionally not used here. These relatively cold,
//! pointer-owning control messages are not the shared atomic hot spots; padding
//! them to 64 bytes would waste memory without guaranteeing L1/L2 residency.

use core::fmt;
use std::mem::size_of;
use std::sync::mpsc::{self, Receiver, SyncSender, TryRecvError, TrySendError};

pub const DEFAULT_UI_COMMAND_CAPACITY: usize = 8;
pub const DEFAULT_UI_EVENT_CAPACITY: usize = 4;
pub const MAX_UI_QUEUE_CAPACITY: usize = 16;
pub const MAX_UI_TEXT_BYTES: usize = 4 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UiLanguage {
    English,
    French,
    Hindi,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UiRuntimeState {
    Idle,
    Listening,
    Processing,
    AwaitingApproval,
    Stopping,
    Faulted,
}

/// Commands initiated by the UI. No arbitrary strings are accepted here; text
/// and prompt content travel through validated service requests instead.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UiCommand {
    StartListening { session_id: u64 },
    StopListening { session_id: u64 },
    SelectLanguage { language: UiLanguage },
    ApproveDraft { draft_id: u64 },
    RejectDraft { draft_id: u64 },
    CancelLlmRequest { request_id: u64 },
    ResetSession { session_id: u64 },
}

/// Validated UI text with a hard UTF-8 byte cap. Construction happens on a
/// control/service thread, never in the audio capture callback.
#[derive(Clone, PartialEq, Eq)]
pub struct UiText(Box<str>);

impl UiText {
    pub fn new(text: impl Into<String>) -> Result<Self, UiTextError> {
        let text = text.into();
        if text.len() > MAX_UI_TEXT_BYTES {
            return Err(UiTextError::TooLarge {
                actual_bytes: text.len(),
                max_bytes: MAX_UI_TEXT_BYTES,
            });
        }
        Ok(Self(text.into_boxed_str()))
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }

    pub fn len(&self) -> usize {
        self.0.len()
    }

    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}

impl fmt::Debug for UiText {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Spoken transcript and finance details must not be copied into logs.
        f.debug_struct("UiText")
            .field("bytes", &self.len())
            .finish()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UiTextError {
    TooLarge {
        actual_bytes: usize,
        max_bytes: usize,
    },
}

impl fmt::Display for UiTextError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::TooLarge {
                actual_bytes,
                max_bytes,
            } => {
                write!(f, "UI text is {actual_bytes} bytes; maximum is {max_bytes}")
            }
        }
    }
}

impl std::error::Error for UiTextError {}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UiFaultCode {
    AudioInputUnavailable,
    AudioOverrun,
    AsrUnavailable,
    LlmUnavailable,
    InvalidCommand,
    WorkingSetExceeded,
    RuntimeFailure,
}

/// Small UI-facing summaries. Detailed DSP events remain inside the engine and
/// are mapped deliberately instead of pushing every large EngineEvent to UI.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UiEventKind {
    StateChanged { state: UiRuntimeState },
    PartialTranscript { text: UiText },
    FinalTranscript { text: UiText },
    DraftReady { draft_id: u64, text: UiText },
    LlmResult { request_id: u64, text: UiText },
    DraftDecisionRecorded { draft_id: u64, approved: bool },
    Fault { code: UiFaultCode, message: UiText },
    AudioOverrun { frames_lost: u64 },
    Metrics(UiMetrics),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UiEvent {
    pub session_id: Option<u64>,
    pub kind: UiEventKind,
}

impl UiEvent {
    pub fn new(session_id: Option<u64>, kind: UiEventKind) -> Self {
        Self { session_id, kind }
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct UiMetrics {
    pub frames_captured: u64,
    pub frames_processed: u64,
    pub native_events: u64,
    pub queue_overruns: u64,
    pub last_frame_latency_us: u32,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct UiBridgeStats {
    pub commands_sent: u64,
    pub commands_received: u64,
    pub events_published: u64,
    pub events_received: u64,
    pub command_queue_full: u64,
    pub event_queue_full: u64,
    pub channel_closed: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UiCommandPoll {
    Command(UiCommand),
    Empty,
    Closed,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UiEventPoll {
    Event(UiEvent),
    Empty,
    Closed,
}

/// Send failure returns the untouched command so the caller must decide how to
/// retry or report it instead of silently losing an approval/stop/reset action.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UiCommandSendError {
    QueueFull(UiCommand),
    RuntimeClosed(UiCommand),
}

impl fmt::Display for UiCommandSendError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::QueueFull(_) => f.write_str("UI command queue is full"),
            Self::RuntimeClosed(_) => f.write_str("runtime command receiver is closed"),
        }
    }
}

impl std::error::Error for UiCommandSendError {}

/// Send failure returns the complete event, including its owned text payload.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UiEventPublishError {
    QueueFull(UiEvent),
    UiClosed(UiEvent),
}

impl fmt::Display for UiEventPublishError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::QueueFull(_) => f.write_str("UI event queue is full"),
            Self::UiClosed(_) => f.write_str("UI event receiver is closed"),
        }
    }
}

impl std::error::Error for UiEventPublishError {}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UiBridgeConfigError {
    InvalidCommandCapacity { requested: usize },
    InvalidEventCapacity { requested: usize },
}

impl fmt::Display for UiBridgeConfigError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidCommandCapacity { requested } => write!(
                f,
                "UI command capacity {requested} must be in 1..={MAX_UI_QUEUE_CAPACITY}"
            ),
            Self::InvalidEventCapacity { requested } => write!(
                f,
                "UI event capacity {requested} must be in 1..={MAX_UI_QUEUE_CAPACITY}"
            ),
        }
    }
}

impl std::error::Error for UiBridgeConfigError {}

/// Runtime-side handle: consumes UI commands and publishes status/results.
pub struct UiBridgeRuntime {
    commands_rx: Receiver<UiCommand>,
    events_tx: SyncSender<UiEvent>,
    command_capacity: usize,
    event_capacity: usize,
    stats: UiBridgeStats,
}

impl UiBridgeRuntime {
    pub fn try_recv_command(&mut self) -> UiCommandPoll {
        match self.commands_rx.try_recv() {
            Ok(command) => {
                self.stats.commands_received = self.stats.commands_received.saturating_add(1);
                UiCommandPoll::Command(command)
            }
            Err(TryRecvError::Empty) => UiCommandPoll::Empty,
            Err(TryRecvError::Disconnected) => UiCommandPoll::Closed,
        }
    }

    /// Non-blocking publication. If the UI is behind, caller receives the exact
    /// event and should apply an explicit policy (retry/coalesce only replaceable
    /// progress events; do not silently discard final transcript/draft results).
    pub fn try_publish_event(&mut self, event: UiEvent) -> Result<(), UiEventPublishError> {
        match self.events_tx.try_send(event) {
            Ok(()) => {
                self.stats.events_published = self.stats.events_published.saturating_add(1);
                Ok(())
            }
            Err(TrySendError::Full(event)) => {
                self.stats.event_queue_full = self.stats.event_queue_full.saturating_add(1);
                Err(UiEventPublishError::QueueFull(event))
            }
            Err(TrySendError::Disconnected(event)) => {
                self.stats.channel_closed = self.stats.channel_closed.saturating_add(1);
                Err(UiEventPublishError::UiClosed(event))
            }
        }
    }

    pub const fn stats(&self) -> UiBridgeStats {
        self.stats
    }

    pub const fn command_capacity(&self) -> usize {
        self.command_capacity
    }

    pub const fn event_capacity(&self) -> usize {
        self.event_capacity
    }

    /// Conservative accounting for bounded message slots and maximum queued
    /// UI text payloads. Standard-library channel bookkeeping/allocator overhead
    /// is implementation-defined, so this is an estimate, not an exact RSS cap.
    pub fn storage_bytes_estimate(&self) -> usize {
        self.command_capacity
            .saturating_mul(size_of::<UiCommand>())
            .saturating_add(self.event_capacity.saturating_mul(size_of::<UiEvent>()))
            .saturating_add(self.event_capacity.saturating_mul(MAX_UI_TEXT_BYTES))
    }
}

/// UI-side handle: publishes commands and consumes runtime events.
pub struct UiBridgeClient {
    commands_tx: SyncSender<UiCommand>,
    events_rx: Receiver<UiEvent>,
    command_capacity: usize,
    event_capacity: usize,
    stats: UiBridgeStats,
}

impl UiBridgeClient {
    pub fn try_send_command(&mut self, command: UiCommand) -> Result<(), UiCommandSendError> {
        match self.commands_tx.try_send(command) {
            Ok(()) => {
                self.stats.commands_sent = self.stats.commands_sent.saturating_add(1);
                Ok(())
            }
            Err(TrySendError::Full(command)) => {
                self.stats.command_queue_full = self.stats.command_queue_full.saturating_add(1);
                Err(UiCommandSendError::QueueFull(command))
            }
            Err(TrySendError::Disconnected(command)) => {
                self.stats.channel_closed = self.stats.channel_closed.saturating_add(1);
                Err(UiCommandSendError::RuntimeClosed(command))
            }
        }
    }

    pub fn try_recv_event(&mut self) -> UiEventPoll {
        match self.events_rx.try_recv() {
            Ok(event) => {
                self.stats.events_received = self.stats.events_received.saturating_add(1);
                UiEventPoll::Event(event)
            }
            Err(TryRecvError::Empty) => UiEventPoll::Empty,
            Err(TryRecvError::Disconnected) => UiEventPoll::Closed,
        }
    }

    pub const fn stats(&self) -> UiBridgeStats {
        self.stats
    }

    pub const fn command_capacity(&self) -> usize {
        self.command_capacity
    }

    pub const fn event_capacity(&self) -> usize {
        self.event_capacity
    }
}

/// Build both bounded directions. No queue is unbounded and no lock or channel
/// operation is performed by the bridge unless a caller explicitly invokes it.
pub fn ui_bridge_channel(
    command_capacity: usize,
    event_capacity: usize,
) -> Result<(UiBridgeRuntime, UiBridgeClient), UiBridgeConfigError> {
    if !(1..=MAX_UI_QUEUE_CAPACITY).contains(&command_capacity) {
        return Err(UiBridgeConfigError::InvalidCommandCapacity {
            requested: command_capacity,
        });
    }
    if !(1..=MAX_UI_QUEUE_CAPACITY).contains(&event_capacity) {
        return Err(UiBridgeConfigError::InvalidEventCapacity {
            requested: event_capacity,
        });
    }

    let (commands_tx, commands_rx) = mpsc::sync_channel(command_capacity);
    let (events_tx, events_rx) = mpsc::sync_channel(event_capacity);
    Ok((
        UiBridgeRuntime {
            commands_rx,
            events_tx,
            command_capacity,
            event_capacity,
            stats: UiBridgeStats::default(),
        },
        UiBridgeClient {
            commands_tx,
            events_rx,
            command_capacity,
            event_capacity,
            stats: UiBridgeStats::default(),
        },
    ))
}

pub fn default_ui_bridge_channel() -> (UiBridgeRuntime, UiBridgeClient) {
    ui_bridge_channel(DEFAULT_UI_COMMAND_CAPACITY, DEFAULT_UI_EVENT_CAPACITY)
        .expect("the default UI queue capacities are valid")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn command_backpressure_returns_original_command() {
        let (mut runtime, mut client) = ui_bridge_channel(1, 1).unwrap();
        let first = UiCommand::StartListening { session_id: 10 };
        let second = UiCommand::StopListening { session_id: 10 };
        client.try_send_command(first).unwrap();
        assert_eq!(
            client.try_send_command(second),
            Err(UiCommandSendError::QueueFull(second))
        );
        assert_eq!(runtime.try_recv_command(), UiCommandPoll::Command(first));
    }

    #[test]
    fn event_backpressure_returns_owned_payload() {
        let (mut runtime, mut client) = ui_bridge_channel(1, 1).unwrap();
        let event = UiEvent::new(
            Some(5),
            UiEventKind::FinalTranscript {
                text: UiText::new("sent rent 1200 rupees").unwrap(),
            },
        );
        let first = UiEvent::new(
            None,
            UiEventKind::StateChanged {
                state: UiRuntimeState::Idle,
            },
        );
        runtime.try_publish_event(first).unwrap();
        match runtime.try_publish_event(event) {
            Err(UiEventPublishError::QueueFull(returned)) => {
                assert_eq!(returned.session_id, Some(5));
                assert!(matches!(returned.kind, UiEventKind::FinalTranscript { .. }));
            }
            other => panic!("unexpected result: {other:?}"),
        }
        assert!(matches!(client.try_recv_event(), UiEventPoll::Event(_)));
    }

    #[test]
    fn oversized_text_is_rejected_and_debug_hides_text() {
        let secret = "account-balance-secret";
        let text = UiText::new(secret).unwrap();
        assert!(!format!("{text:?}").contains(secret));
        let too_large = "x".repeat(MAX_UI_TEXT_BYTES + 1);
        assert!(matches!(
            UiText::new(too_large),
            Err(UiTextError::TooLarge { .. })
        ));
    }
}
