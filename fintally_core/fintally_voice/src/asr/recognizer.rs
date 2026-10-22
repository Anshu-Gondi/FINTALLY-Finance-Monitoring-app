//! Bounded incremental ASR worker.
//!
//! Audio capture calls `try_submit_pcm16` and never waits for inference. Backend
//! ownership stays on a dedicated worker; queue payloads and transcript events
//! are fixed-size and all queues are bounded. See the child modules for the
//! runtime API, fixed message types, and worker implementation.

mod runtime;
mod types;
mod worker;

#[cfg(test)]
mod tests;

pub use runtime::AsrRuntime;
pub use types::{
    AsrAudioFrame, AsrControlError, AsrEvent, AsrEventKind, AsrMemoryReport, AsrRuntimeConfig,
    AsrStatsSnapshot, AsrSubmitFrameError, AudioSubmitError, APPLICATION_RUNTIME_BUDGET_BYTES,
    ASR_RUNTIME_BUDGET_BYTES, DEFAULT_ASR_CONTROL_CAPACITY, DEFAULT_ASR_INPUT_CAPACITY,
    DEFAULT_ASR_OUTPUT_CAPACITY,
};
