//! Happy-path and fail-closed tests for deterministic financial intent extraction.

use fintally_voice::intent::{
    classify_intent, IntentDecision, IntentKind, MissingSlot, MAX_INTENT_TEXT_BYTES,
};
use fintally_voice::transcript::TranscriptLanguage;

#[test]
fn known_read_only_request_is_not_a_mutation() {
    let intent = classify_intent("check balance", TranscriptLanguage::English)
        .expect("short bounded request should classify");

    assert_eq!(intent.kind, IntentKind::CheckBalance);
    assert_eq!(intent.decision, IntentDecision::ReadOnly);
    assert!(!intent.negated);
    assert!(!intent.ambiguous);
    assert!(intent.confidence >= 0.80);
    assert!(intent.missing_slots.is_empty());
}

#[test]
fn complete_transfer_is_confirmation_gated_and_preserves_source_spans() {
    let source = "transfer 1,250.75 to Rahul";
    let intent = classify_intent(source, TranscriptLanguage::English)
        .expect("bounded transfer should classify");

    assert_eq!(intent.kind, IntentKind::TransferMoney);
    assert_eq!(intent.decision, IntentDecision::NeedsConfirmation);
    assert!(intent.slots.amount.is_some());
    assert!(intent.slots.payee.is_some());
    assert_eq!(
        intent.slots.amount.and_then(|span| span.resolve(source)),
        Some("1,250.75")
    );
    assert_eq!(
        intent.slots.payee.and_then(|span| span.resolve(source)),
        Some("Rahul")
    );
    assert!(intent.missing_slots.is_empty());
}

#[test]
fn negated_transfer_is_never_routed_to_confirmation() {
    let intent = classify_intent("do not transfer 250 to Rahul", TranscriptLanguage::English)
        .expect("negated bounded request should classify conservatively");

    assert_eq!(intent.kind, IntentKind::TransferMoney);
    assert!(intent.negated);
    assert_eq!(intent.decision, IntentDecision::NeedsClarification);
}

#[test]
fn incomplete_transfer_exposes_missing_amount_and_destination() {
    let intent = classify_intent("transfer money", TranscriptLanguage::English)
        .expect("incomplete request should return a structured result");

    assert_eq!(intent.kind, IntentKind::TransferMoney);
    assert_eq!(intent.decision, IntentDecision::NeedsClarification);
    assert!(intent.missing_slots.contains(MissingSlot::Amount));
    assert!(intent.missing_slots.contains(MissingSlot::Destination));
}

#[test]
fn unknown_language_cannot_gain_high_confidence_from_english_rules() {
    let intent = classify_intent("check balance", TranscriptLanguage::Unknown)
        .expect("unknown language is a supported conservative input");

    assert_eq!(intent.kind, IntentKind::CheckBalance);
    assert!(intent.confidence <= 0.69);
    assert_eq!(intent.decision, IntentDecision::NeedsClarification);
}

#[test]
fn unsupported_request_fails_closed_without_inventing_an_intent() {
    let intent = classify_intent("play a song", TranscriptLanguage::English)
        .expect("unsupported request is not an input error");

    assert_eq!(intent.kind, IntentKind::Unknown);
    assert_eq!(intent.decision, IntentDecision::NoMatch);
    assert_eq!(intent.confidence, 0.0);
    assert!(intent.slots.amount.is_none());
    assert!(intent.slots.payee.is_none());
}

#[test]
fn oversized_input_is_rejected_before_classification() {
    let source = "x".repeat(MAX_INTENT_TEXT_BYTES + 1);
    let error = classify_intent(&source, TranscriptLanguage::English)
        .expect_err("oversized intent input must fail closed");

    assert!(matches!(
        error,
        fintally_voice::intent::ClassificationError::InputTooLong { .. }
    ));
}

#[test]
fn multilingual_hindi_balance_phrase_is_recognized_as_read_only() {
    let intent = classify_intent("बैलेंस चेक", TranscriptLanguage::Hindi)
        .expect("bounded Hindi read-only request should classify");

    assert_eq!(intent.kind, IntentKind::CheckBalance);
    assert_eq!(intent.decision, IntentDecision::ReadOnly);
}

#[test]
fn transfer_payee_excludes_unrelated_trailing_clause() {
    let input = "transfer 250 to Rahul and rust is good";

    let result = classify_intent(input, TranscriptLanguage::English)
        .expect("input is within the classifier limit");

    assert_eq!(result.kind, IntentKind::TransferMoney);

    assert_eq!(
        result.slots.amount.and_then(|span| span.resolve(input)),
        Some("250")
    );

    assert_eq!(
        result.slots.payee.and_then(|span| span.resolve(input)),
        Some("Rahul"),
        "unrelated trailing speech must not contaminate the payee"
    );
}
