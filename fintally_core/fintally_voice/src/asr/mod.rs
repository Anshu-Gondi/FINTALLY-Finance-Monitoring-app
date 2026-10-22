//! Automatic speech recognition subsystem.
//!
//! `backend` owns backend contracts/configuration and the optional Sherpa-ONNX
//! adapter. `recognizer` owns the bounded input/output queues and the dedicated
//! model worker. Neither module is part of the C ABI; ASR outputs are ordinary
//! Rust events and should be adapted at the application/UI boundary.

pub mod backend;
pub mod recognizer;

pub use backend::{
    AsrBackend, AsrBackendConfig, AsrError, BackendTranscript, SherpaOnlineTransducerConfig,
    TranscriptKind, ASR_FRAME_SAMPLES, ASR_SAMPLE_RATE_HZ, MAX_TRANSCRIPT_BYTES,
};
pub use recognizer::{
    AsrAudioFrame, AsrControlError, AsrEvent, AsrEventKind, AsrMemoryReport, AsrRuntime,
    AsrRuntimeConfig, AsrStatsSnapshot, AsrSubmitFrameError, AudioSubmitError,
    APPLICATION_RUNTIME_BUDGET_BYTES, ASR_RUNTIME_BUDGET_BYTES, DEFAULT_ASR_CONTROL_CAPACITY,
    DEFAULT_ASR_INPUT_CAPACITY, DEFAULT_ASR_OUTPUT_CAPACITY,
};
