//! Deterministic intent classification and financial-command safety gates.
//!
//! Classify a transcript, inspect its zero-copy slots and missing fields, then
//! honor `IntentDecision`. This module does not execute financial operations.

mod classify;
mod rules;
mod slots;

pub use classify::{
    classify_intent, ClassificationError, Currency, ExtractedSlots, IntentDecision, IntentKind,
    IntentResult, MissingSlot, MissingSlots, TextSpan, MAX_INTENT_TEXT_BYTES, MIN_CONFIDENCE,
};
