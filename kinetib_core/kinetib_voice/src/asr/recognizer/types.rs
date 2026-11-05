//! Fixed-capacity messages, validation, queue sizing, and ASR runtime counters.

use crate::asr::backend::{
    AsrError, BackendTranscript, TranscriptKind, ASR_FRAME_SAMPLES, ASR_SAMPLE_RATE_HZ,
    MAX_TRANSCRIPT_BYTES,
};
use std::fmt;
use std::mem::size_of;
use std::sync::atomic::{AtomicU64, Ordering};
use thiserror::Error;

pub const DEFAULT_ASR_INPUT_CAPACITY: usize = 4;
pub const DEFAULT_ASR_OUTPUT_CAPACITY: usize = 8;
pub const DEFAULT_ASR_CONTROL_CAPACITY: usize = 4;
/// Reserved sub-budget for the bounded ASR runtime, within the app-wide 100 KiB
/// DSP/runtime working-set envelope. Model weights/native ASR allocations are separate.
pub const ASR_RUNTIME_BUDGET_BYTES: usize = 16 * 1024;
pub const APPLICATION_RUNTIME_BUDGET_BYTES: usize = 100 * 1024;
const MIN_QUEUE_CAPACITY: usize = 2;
pub(super) const MAX_INPUT_CAPACITY: usize = 32;
const MAX_OUTPUT_CAPACITY: usize = 32;
const MAX_CONTROL_CAPACITY: usize = 16;
const QUEUE_SLOT_OVERHEAD_ESTIMATE: usize = 64;

/// Fixed-size input frame. Its 64-byte alignment helps keep adjacent channel
/// slots from sharing cache lines when the channel stores arrays of this type.
/// This is false-sharing mitigation, not a guarantee that data stays in L1/L2.
#[repr(align(64))]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct AsrAudioFrame {
    pub session_id: u64,
    pub timestamp_ns: u64,
    pub sample_position: u64,
    pub samples: [i16; ASR_FRAME_SAMPLES],
}

impl fmt::Debug for AsrAudioFrame {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("AsrAudioFrame")
            .field("session_id", &self.session_id)
            .field("timestamp_ns", &self.timestamp_ns)
            .field("sample_position", &self.sample_position)
            .field("sample_count", &self.samples.len())
            .finish_non_exhaustive()
    }
}

impl AsrAudioFrame {
    pub fn new(
        session_id: u64,
        timestamp_ns: u64,
        sample_position: u64,
        samples: &[i16],
    ) -> Result<Self, AsrError> {
        if session_id == 0 {
            return Err(AsrError::InvalidConfig(
                "session_id must be non-zero and monotonically increasing",
            ));
        }
        if samples.len() != ASR_FRAME_SAMPLES {
            return Err(AsrError::InvalidFrameLength {
                expected: ASR_FRAME_SAMPLES,
                actual: samples.len(),
            });
        }
        let mut frame = Self {
            session_id,
            timestamp_ns,
            sample_position,
            samples: [0; ASR_FRAME_SAMPLES],
        };
        frame.samples.copy_from_slice(samples);
        Ok(frame)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AsrEventKind {
    PartialTranscript,
    FinalTranscript,
    SessionCancelled,
    SessionFinished,
    TranscriptTooLong,
    BackendFailure,
    WorkerStopped,
}

/// Fixed output event. `repr(align(64))` is used because events are transferred
/// across worker/UI boundaries frequently; the fixed payload avoids per-event
/// allocation in this Rust layer.
#[repr(align(64))]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct AsrEvent {
    pub session_id: u64,
    pub timestamp_ns: u64,
    pub sample_position: u64,
    pub kind: AsrEventKind,
    text_len: u16,
    text: [u8; MAX_TRANSCRIPT_BYTES],
}

impl fmt::Debug for AsrEvent {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Do not print financial transcripts into logs by default.
        f.debug_struct("AsrEvent")
            .field("session_id", &self.session_id)
            .field("timestamp_ns", &self.timestamp_ns)
            .field("sample_position", &self.sample_position)
            .field("kind", &self.kind)
            .field("text_len", &self.text_len)
            .finish()
    }
}

impl AsrEvent {
    pub(super) fn marker(
        session_id: u64,
        timestamp_ns: u64,
        sample_position: u64,
        kind: AsrEventKind,
    ) -> Self {
        Self {
            session_id,
            timestamp_ns,
            sample_position,
            kind,
            text_len: 0,
            text: [0; MAX_TRANSCRIPT_BYTES],
        }
    }

    pub(super) fn transcript(
        session_id: u64,
        timestamp_ns: u64,
        sample_position: u64,
        update: BackendTranscript<'_>,
    ) -> Self {
        if update.text.len() > MAX_TRANSCRIPT_BYTES {
            return Self::marker(
                session_id,
                timestamp_ns,
                sample_position,
                AsrEventKind::TranscriptTooLong,
            );
        }
        let mut event = Self::marker(
            session_id,
            timestamp_ns,
            sample_position,
            match update.kind {
                TranscriptKind::Partial => AsrEventKind::PartialTranscript,
                TranscriptKind::Final => AsrEventKind::FinalTranscript,
            },
        );
        event.text[..update.text.len()].copy_from_slice(update.text.as_bytes());
        event.text_len = update.text.len() as u16;
        event
    }

    /// Return text only for transcript events. Invalid UTF-8 can never be
    /// produced by the writer, but the read still checks rather than assuming.
    pub fn text(&self) -> Option<&str> {
        match self.kind {
            AsrEventKind::PartialTranscript | AsrEventKind::FinalTranscript => {
                std::str::from_utf8(&self.text[..usize::from(self.text_len)]).ok()
            }
            _ => None,
        }
    }
}

#[derive(Debug, Clone)]
pub struct AsrRuntimeConfig {
    pub sample_rate_hz: u32,
    pub input_capacity: usize,
    pub output_capacity: usize,
    pub control_capacity: usize,
    pub memory_budget_bytes: usize,
}

impl Default for AsrRuntimeConfig {
    fn default() -> Self {
        Self {
            sample_rate_hz: ASR_SAMPLE_RATE_HZ,
            input_capacity: DEFAULT_ASR_INPUT_CAPACITY,
            output_capacity: DEFAULT_ASR_OUTPUT_CAPACITY,
            control_capacity: DEFAULT_ASR_CONTROL_CAPACITY,
            memory_budget_bytes: ASR_RUNTIME_BUDGET_BYTES,
        }
    }
}

impl AsrRuntimeConfig {
    pub fn validate(&self) -> Result<(), AsrError> {
        if self.sample_rate_hz != ASR_SAMPLE_RATE_HZ {
            return Err(AsrError::UnsupportedSampleRate {
                expected: ASR_SAMPLE_RATE_HZ,
                actual: self.sample_rate_hz,
            });
        }
        if !(MIN_QUEUE_CAPACITY..=MAX_INPUT_CAPACITY).contains(&self.input_capacity) {
            return Err(AsrError::InvalidConfig("input_capacity must be in 2..=32"));
        }
        if !(MIN_QUEUE_CAPACITY..=MAX_OUTPUT_CAPACITY).contains(&self.output_capacity) {
            return Err(AsrError::InvalidConfig("output_capacity must be in 2..=32"));
        }
        if !(MIN_QUEUE_CAPACITY..=MAX_CONTROL_CAPACITY).contains(&self.control_capacity) {
            return Err(AsrError::InvalidConfig(
                "control_capacity must be in 2..=16",
            ));
        }
        if self.memory_budget_bytes == 0 || self.memory_budget_bytes > ASR_RUNTIME_BUDGET_BYTES {
            return Err(AsrError::InvalidConfig(
                "memory_budget_bytes must be in 1..=16384; this is a reservation within the app-wide 100 KiB budget",
            ));
        }
        let estimated = self.estimated_runtime_bytes();
        if estimated > self.memory_budget_bytes {
            return Err(AsrError::MemoryBudgetExceeded {
                estimated,
                budget: self.memory_budget_bytes,
            });
        }
        Ok(())
    }

    /// Planning estimate of Rust-owned fixed payloads, an allowance for bounded
    /// channel slot overhead, and reusable float conversion scratch. It is not a
    /// measured RSS guarantee and excludes backend-native allocations/workspaces.
    pub fn estimated_runtime_bytes(&self) -> usize {
        self.input_capacity
            .saturating_mul(size_of::<AsrAudioFrame>())
            .saturating_add(self.output_capacity.saturating_mul(size_of::<AsrEvent>()))
            .saturating_add(
                self.control_capacity
                    .saturating_mul(size_of::<ControlCommand>()),
            )
            .saturating_add(
                self.input_capacity
                    .saturating_add(self.output_capacity)
                    .saturating_add(self.control_capacity)
                    .saturating_mul(QUEUE_SLOT_OVERHEAD_ESTIMATE),
            )
            .saturating_add(size_of::<[f32; ASR_FRAME_SAMPLES]>())
            .saturating_add(size_of::<RuntimeCounters>())
    }

    pub fn memory_report(&self) -> AsrMemoryReport {
        AsrMemoryReport {
            estimated_rust_runtime_bytes: self.estimated_runtime_bytes(),
            budget_bytes: self.memory_budget_bytes,
            asr_model_weights_and_native_backend_excluded: true,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AsrMemoryReport {
    pub estimated_rust_runtime_bytes: usize,
    pub budget_bytes: usize,
    pub asr_model_weights_and_native_backend_excluded: bool,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct AsrStatsSnapshot {
    pub frames_accepted: u64,
    pub frames_processed: u64,
    pub input_queue_full: u64,
    pub stale_frames_ignored: u64,
    pub backend_failures: u64,
    pub events_published: u64,
    /// Total worker service time across frames, including event handoff/backpressure.
    pub frame_service_time_ns_total: u64,
    /// Maximum observed frame service time (inference plus event handoff).
    pub frame_service_time_ns_max: u64,
}

/// Producer-written counters occupy their own cache line, separated from the
/// worker-written counters below to reduce cache-line ownership ping-pong.
#[repr(align(64))]
#[derive(Default)]
pub(super) struct ProducerCounters {
    pub(super) frames_accepted: AtomicU64,
    pub(super) input_queue_full: AtomicU64,
}

/// Worker-written counters are grouped on a separate 64-byte-aligned region.
#[repr(align(64))]
#[derive(Default)]
pub(super) struct WorkerCounters {
    pub(super) frames_processed: AtomicU64,
    pub(super) stale_frames_ignored: AtomicU64,
    pub(super) backend_failures: AtomicU64,
    pub(super) events_published: AtomicU64,
    pub(super) frame_service_time_ns_total: AtomicU64,
    pub(super) frame_service_time_ns_max: AtomicU64,
}

#[derive(Default)]
pub(super) struct RuntimeCounters {
    pub(super) producer: ProducerCounters,
    pub(super) worker: WorkerCounters,
}

impl RuntimeCounters {
    pub(super) fn snapshot(&self) -> AsrStatsSnapshot {
        AsrStatsSnapshot {
            frames_accepted: self.producer.frames_accepted.load(Ordering::Relaxed),
            frames_processed: self.worker.frames_processed.load(Ordering::Relaxed),
            input_queue_full: self.producer.input_queue_full.load(Ordering::Relaxed),
            stale_frames_ignored: self.worker.stale_frames_ignored.load(Ordering::Relaxed),
            backend_failures: self.worker.backend_failures.load(Ordering::Relaxed),
            events_published: self.worker.events_published.load(Ordering::Relaxed),
            frame_service_time_ns_total: self
                .worker
                .frame_service_time_ns_total
                .load(Ordering::Relaxed),
            frame_service_time_ns_max: self
                .worker
                .frame_service_time_ns_max
                .load(Ordering::Relaxed),
        }
    }
}

#[derive(Debug, Error)]
pub enum AudioSubmitError {
    #[error("ASR audio queue is full; the original frame is returned")]
    QueueFull(AsrAudioFrame),
    #[error("ASR worker is no longer accepting audio; the original frame is returned")]
    WorkerStopped(AsrAudioFrame),
}

impl AudioSubmitError {
    pub fn into_frame(self) -> AsrAudioFrame {
        match self {
            Self::QueueFull(frame) | Self::WorkerStopped(frame) => frame,
        }
    }
}

#[derive(Debug, Error)]
pub enum AsrControlError {
    #[error("ASR control queue is full")]
    QueueFull,
    #[error("ASR worker is disconnected")]
    WorkerStopped,
}

pub(super) enum ControlCommand {
    Cancel { session_id: u64 },
    Finish { session_id: u64 },
    Shutdown,
}

#[derive(Debug, Error)]
pub enum AsrSubmitFrameError {
    #[error(transparent)]
    InvalidFrame(#[from] AsrError),
    #[error(transparent)]
    Queue(#[from] AudioSubmitError),
}

impl AsrSubmitFrameError {
    /// Recover an input frame when it was rejected by the bounded queue. Invalid
    /// frame construction has no recoverable `AsrAudioFrame` value.
    pub fn into_frame(self) -> Option<AsrAudioFrame> {
        match self {
            Self::InvalidFrame(_) => None,
            Self::Queue(error) => Some(error.into_frame()),
        }
    }
}
