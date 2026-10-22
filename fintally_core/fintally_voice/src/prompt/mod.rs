//! Bounded prompt construction and validation for FinTally Voice.
//!
//! The prompt layer may suggest wording or ask a clarification question. It is
//! never an execution engine or the source of truth for financial values.

mod draft;

pub use draft::{
    build_prompt_draft, DraftMode, DraftSuggestion, PromptDraft, PromptError, SuggestionStatus,
    MAX_MODEL_SUGGESTION_BYTES, MAX_PROMPT_BYTES, MAX_SUGGESTION_MESSAGE_BYTES,
    MAX_SUGGESTION_QUESTION_BYTES, MAX_SUGGESTION_RISK_NOTE_BYTES,
};
