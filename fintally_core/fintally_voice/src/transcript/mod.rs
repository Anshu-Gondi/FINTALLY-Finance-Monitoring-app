//! Transcript retention and conservative cleanup API.
mod cleanup;
pub use cleanup::{
    clean_transcript, CleanupPolicy, Transcript, TranscriptError, TranscriptLanguage,
    MAX_CLEAN_TRANSCRIPT_BYTES, MAX_FILLER_TOKENS, MAX_RAW_TRANSCRIPT_BYTES, MAX_TOKEN_BYTES,
};
