//! FinTally Voice — Rust orchestration and native audio core integration.
//!
//! Responsibilities:
//! - Expose the voice-engine modules.
//! - Keep native DSP operations behind the FFI boundary.
//! - Keep ASR, transcript processing, intent classification and prompt
//!   approval in Rust.
//!
//! Real-time audio processing must use bounded buffers and queues.

pub mod asr;
pub mod engine;
pub mod event;
pub mod ffi;
pub mod fin_voice_codebooks;
pub mod intent;
pub mod prompt;
pub mod threads;
pub mod transcript;
