//! Bounded control-plane handoff to an LLM-backed service.
//!
//! This module is deliberately outside the audio/DSP hot path. Build requests,
//! call the model/backend, and publish completions only on a service thread.
//! The request queue is bounded; submission is non-blocking and returns the
//! original request on backpressure. It does not perform network I/O itself.
//!
//! Payload lengths are capped so queue capacity also bounds queued payload
//! memory. LLM model weights, HTTP/TLS buffers, and backend-owned memory are
//! separate budgets and are not included in the 100 KiB DSP working-set report.

use core::fmt;
use std::sync::mpsc::{self, Receiver, SyncSender, TrySendError};
use std::time::{Duration, Instant};

pub const DEFAULT_CONTROL_LLM_QUEUE_CAPACITY: usize = 2;
pub const MAX_CONTROL_LLM_QUEUE_CAPACITY: usize = 4;
pub const MAX_RAW_TRANSCRIPT_BYTES: usize = 2 * 1024;
pub const MAX_LLM_PROMPT_BYTES: usize = 4 * 1024;
pub const MAX_LLM_OUTPUT_BYTES: usize = 4 * 1024;
pub const MAX_LLM_REQUEST_PAYLOAD_BYTES: usize = MAX_RAW_TRANSCRIPT_BYTES + MAX_LLM_PROMPT_BYTES;

/// Application-level request identity. It is not a C ABI type.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct LlmRequestId(pub u64);

/// Owned LLM work item. Its Debug implementation never prints spoken text or
/// prompt contents, which may contain private financial information.
pub struct LlmRequest {
    id: LlmRequestId,
    raw_transcript: Box<str>,
    prompt: Box<str>,
}

impl LlmRequest {
    /// Validate and own a complete request. This may allocate and must not be
    /// called from an audio callback or the DSP worker.
    pub fn new(
        id: LlmRequestId,
        raw_transcript: impl Into<String>,
        prompt: impl Into<String>,
    ) -> Result<Self, LlmRequestValidationError> {
        let raw_transcript = raw_transcript.into();
        let prompt = prompt.into();

        if raw_transcript.is_empty() {
            return Err(LlmRequestValidationError::EmptyTranscript);
        }
        if prompt.trim().is_empty() {
            return Err(LlmRequestValidationError::EmptyPrompt);
        }
        if raw_transcript.len() > MAX_RAW_TRANSCRIPT_BYTES {
            return Err(LlmRequestValidationError::TranscriptTooLarge {
                actual_bytes: raw_transcript.len(),
                max_bytes: MAX_RAW_TRANSCRIPT_BYTES,
            });
        }
        if prompt.len() > MAX_LLM_PROMPT_BYTES {
            return Err(LlmRequestValidationError::PromptTooLarge {
                actual_bytes: prompt.len(),
                max_bytes: MAX_LLM_PROMPT_BYTES,
            });
        }

        Ok(Self {
            id,
            // Preserve transcript bytes exactly; do not normalize amounts,
            // currency symbols, negations, account names, or other semantics.
            raw_transcript: raw_transcript.into_boxed_str(),
            prompt: prompt.into_boxed_str(),
        })
    }

    pub const fn id(&self) -> LlmRequestId {
        self.id
    }

    pub fn raw_transcript(&self) -> &str {
        &self.raw_transcript
    }

    pub fn prompt(&self) -> &str {
        &self.prompt
    }

    pub fn payload_bytes(&self) -> usize {
        self.raw_transcript.len().saturating_add(self.prompt.len())
    }
}

impl fmt::Debug for LlmRequest {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("LlmRequest")
            .field("id", &self.id)
            .field("transcript_bytes", &self.raw_transcript.len())
            .field("prompt_bytes", &self.prompt.len())
            .finish_non_exhaustive()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LlmRequestValidationError {
    EmptyTranscript,
    EmptyPrompt,
    TranscriptTooLarge {
        actual_bytes: usize,
        max_bytes: usize,
    },
    PromptTooLarge {
        actual_bytes: usize,
        max_bytes: usize,
    },
}

impl fmt::Display for LlmRequestValidationError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::EmptyTranscript => f.write_str("LLM request transcript is empty"),
            Self::EmptyPrompt => f.write_str("LLM request prompt is empty"),
            Self::TranscriptTooLarge {
                actual_bytes,
                max_bytes,
            } => {
                write!(
                    f,
                    "transcript is {actual_bytes} bytes; maximum is {max_bytes}"
                )
            }
            Self::PromptTooLarge {
                actual_bytes,
                max_bytes,
            } => {
                write!(f, "prompt is {actual_bytes} bytes; maximum is {max_bytes}")
            }
        }
    }
}

impl std::error::Error for LlmRequestValidationError {}

/// Submission errors return the unqueued request so the caller can retry,
/// surface backpressure, or explicitly abandon it. Never silently drop it.
pub enum LlmSubmitError {
    QueueFull(LlmRequest),
    WorkerClosed(LlmRequest),
}

impl LlmSubmitError {
    pub fn into_request(self) -> LlmRequest {
        match self {
            Self::QueueFull(request) | Self::WorkerClosed(request) => request,
        }
    }
}

impl fmt::Debug for LlmSubmitError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::QueueFull(request) => f.debug_tuple("QueueFull").field(request).finish(),
            Self::WorkerClosed(request) => f.debug_tuple("WorkerClosed").field(request).finish(),
        }
    }
}

impl fmt::Display for LlmSubmitError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::QueueFull(_) => f.write_str("LLM control queue is full"),
            Self::WorkerClosed(_) => f.write_str("LLM control worker is closed"),
        }
    }
}

impl std::error::Error for LlmSubmitError {}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LlmBackendError {
    Unavailable,
    Timeout,
    Rejected,
    InvalidResponse,
    Cancelled,
    EmptyOutput,
    OutputTooLarge {
        actual_bytes: usize,
        max_bytes: usize,
    },
    Internal,
}

/// Completion from the backend. Debug reports status and payload size only.
pub struct LlmCompletion {
    id: LlmRequestId,
    result: Result<Box<str>, LlmBackendError>,
    elapsed: Duration,
}

impl LlmCompletion {
    pub const fn id(&self) -> LlmRequestId {
        self.id
    }

    pub fn result(&self) -> Result<&str, LlmBackendError> {
        self.result.as_deref().map_err(|error| *error)
    }

    pub fn into_result(self) -> Result<Box<str>, LlmBackendError> {
        self.result
    }

    pub const fn elapsed(&self) -> Duration {
        self.elapsed
    }

    pub fn output_bytes(&self) -> usize {
        self.result.as_ref().map_or(0, |text| text.len())
    }
}

impl fmt::Debug for LlmCompletion {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("LlmCompletion")
            .field("id", &self.id)
            .field("ok", &self.result.is_ok())
            .field("output_bytes", &self.output_bytes())
            .field("error", &self.result.as_ref().err())
            .field("elapsed", &self.elapsed)
            .finish()
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LlmSubmitStats {
    pub accepted: u64,
    pub queue_full: u64,
    pub worker_closed: u64,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LlmWorkerStats {
    pub requests_received: u64,
    pub requests_completed: u64,
    pub backend_failures: u64,
    pub delivery_backpressure: u64,
}

/// Single control-plane producer endpoint. It intentionally is not Clone:
/// keep one logical orchestrator as the producer for predictable ordering.
pub struct ControlLlmSender {
    tx: SyncSender<LlmRequest>,
    capacity: usize,
    stats: LlmSubmitStats,
}

impl ControlLlmSender {
    /// Non-blocking bounded submission. Request construction has already done
    /// any required allocation, so this method does not wait for queue space.
    pub fn try_submit(&mut self, request: LlmRequest) -> Result<(), LlmSubmitError> {
        match self.tx.try_send(request) {
            Ok(()) => {
                self.stats.accepted = self.stats.accepted.saturating_add(1);
                Ok(())
            }
            Err(TrySendError::Full(request)) => {
                self.stats.queue_full = self.stats.queue_full.saturating_add(1);
                Err(LlmSubmitError::QueueFull(request))
            }
            Err(TrySendError::Disconnected(request)) => {
                self.stats.worker_closed = self.stats.worker_closed.saturating_add(1);
                Err(LlmSubmitError::WorkerClosed(request))
            }
        }
    }

    pub const fn capacity(&self) -> usize {
        self.capacity
    }

    pub const fn stats(&self) -> LlmSubmitStats {
        self.stats
    }

    /// Maximum queued transcript+prompt payload; excludes channel metadata
    /// and the one request currently being processed by the worker.
    pub fn queued_payload_budget_bytes(&self) -> usize {
        self.capacity.saturating_mul(MAX_LLM_REQUEST_PAYLOAD_BYTES)
    }
}

/// Single control-plane consumer endpoint. Run it on a service thread, never on
/// the real-time DSP worker. If `deliver` cannot accept a completion, it must
/// return that same completion; `run` returns it without dropping it and leaves
/// the worker/remaining queue available for a retry.
pub struct ControlLlmWorker {
    rx: Receiver<LlmRequest>,
    capacity: usize,
    stats: LlmWorkerStats,
}

impl ControlLlmWorker {
    /// Receives until all senders are dropped. The backend closure should have
    /// its own network/model timeout: Rust cannot safely pre-empt a blocked call.
    pub fn run<P, D>(&mut self, processor: &mut P, deliver: &mut D) -> Result<(), LlmCompletion>
    where
        P: FnMut(&LlmRequest) -> Result<String, LlmBackendError>,
        D: FnMut(LlmCompletion) -> Result<(), LlmCompletion>,
    {
        loop {
            let request = match self.rx.recv() {
                Ok(request) => request,
                Err(_) => return Ok(()),
            };
            self.stats.requests_received = self.stats.requests_received.saturating_add(1);

            let request_id = request.id();
            let started = Instant::now();
            let backend_result = processor(&request);
            let result = match backend_result {
                Ok(output) if output.is_empty() => Err(LlmBackendError::EmptyOutput),
                Ok(output) if output.len() > MAX_LLM_OUTPUT_BYTES => {
                    Err(LlmBackendError::OutputTooLarge {
                        actual_bytes: output.len(),
                        max_bytes: MAX_LLM_OUTPUT_BYTES,
                    })
                }
                Ok(output) => Ok(output.into_boxed_str()),
                Err(error) => Err(error),
            };
            if result.is_err() {
                self.stats.backend_failures = self.stats.backend_failures.saturating_add(1);
            }
            let completion = LlmCompletion {
                id: request_id,
                result,
                elapsed: started.elapsed(),
            };
            self.stats.requests_completed = self.stats.requests_completed.saturating_add(1);

            if let Err(completion) = deliver(completion) {
                self.stats.delivery_backpressure =
                    self.stats.delivery_backpressure.saturating_add(1);
                return Err(completion);
            }
        }
    }

    pub const fn capacity(&self) -> usize {
        self.capacity
    }

    pub const fn stats(&self) -> LlmWorkerStats {
        self.stats
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ControlLlmConfigError {
    InvalidCapacity {
        requested: usize,
        min: usize,
        max: usize,
    },
}

impl fmt::Display for ControlLlmConfigError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidCapacity {
                requested,
                min,
                max,
            } => {
                write!(f, "LLM queue capacity {requested} is outside {min}..={max}")
            }
        }
    }
}

impl std::error::Error for ControlLlmConfigError {}

/// Build the bounded handoff. Dropping the sender closes the stream after all
/// already-queued requests have been received by the worker.
pub fn control_llm_channel(
    capacity: usize,
) -> Result<(ControlLlmSender, ControlLlmWorker), ControlLlmConfigError> {
    if !(1..=MAX_CONTROL_LLM_QUEUE_CAPACITY).contains(&capacity) {
        return Err(ControlLlmConfigError::InvalidCapacity {
            requested: capacity,
            min: 1,
            max: MAX_CONTROL_LLM_QUEUE_CAPACITY,
        });
    }
    let (tx, rx) = mpsc::sync_channel(capacity);
    Ok((
        ControlLlmSender {
            tx,
            capacity,
            stats: LlmSubmitStats::default(),
        },
        ControlLlmWorker {
            rx,
            capacity,
            stats: LlmWorkerStats::default(),
        },
    ))
}

pub fn default_control_llm_channel() -> (ControlLlmSender, ControlLlmWorker) {
    // The constant is validated by the constructor; this cannot fail.
    control_llm_channel(DEFAULT_CONTROL_LLM_QUEUE_CAPACITY)
        .expect("the default LLM queue capacity is valid")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn request(id: u64) -> LlmRequest {
        LlmRequest::new(
            LlmRequestId(id),
            "add 12 dollars",
            "Create a structured finance action",
        )
        .expect("test request is valid")
    }

    #[test]
    fn queue_full_returns_original_request() {
        let (mut sender, _worker) = control_llm_channel(1).unwrap();
        sender.try_submit(request(1)).unwrap();
        let error = sender.try_submit(request(2)).unwrap_err();
        match error {
            LlmSubmitError::QueueFull(returned) => assert_eq!(returned.id(), LlmRequestId(2)),
            other => panic!("unexpected submit error: {other:?}"),
        }
    }

    #[test]
    fn request_debug_does_not_leak_transcript_or_prompt() {
        let request = LlmRequest::new(
            LlmRequestId(7),
            "secret account balance 99123",
            "secret system instruction",
        )
        .unwrap();
        let debug = format!("{request:?}");
        assert!(!debug.contains("99123"));
        assert!(!debug.contains("secret"));
        assert!(debug.contains("transcript_bytes"));
    }

    #[test]
    fn rejects_oversized_output_without_truncation() {
        let (mut sender, mut worker) = control_llm_channel(1).unwrap();
        sender.try_submit(request(3)).unwrap();
        let (done_tx, done_rx) = std::sync::mpsc::channel();
        let worker_thread = std::thread::spawn(move || {
            let mut processor = |_request: &LlmRequest| Ok("x".repeat(MAX_LLM_OUTPUT_BYTES + 1));
            let mut deliver = |completion: LlmCompletion| {
                done_tx.send(completion.result().map(str::len)).unwrap();
                Ok(())
            };
            worker.run(&mut processor, &mut deliver).unwrap();
        });
        drop(sender);
        assert!(matches!(
            done_rx.recv().unwrap(),
            Err(LlmBackendError::OutputTooLarge { .. })
        ));
        worker_thread.join().unwrap();
    }
}
