//! Integration coverage for transcript retention and conservative cleanup.
//!
//! These tests protect financial tokens, negation, and the original transcript.
//! Cleanup is not allowed to make a dangerous command look affirmative.

use fintally_voice::transcript::{
    clean_transcript, CleanupPolicy, TranscriptError, TranscriptLanguage, MAX_RAW_TRANSCRIPT_BYTES,
    MAX_TOKEN_BYTES,
};

#[test]
fn removes_only_known_standalone_english_fillers_and_keeps_raw_text() {
    let raw = "Um, do not transfer uh 1,250.75 to North Star Bank!";
    let transcript = clean_transcript(
        raw,
        TranscriptLanguage::English,
        CleanupPolicy::Conservative,
    )
    .expect("valid transcript should be cleaned");

    assert_eq!(transcript.raw(), raw);
    assert_eq!(
        transcript.cleaned(),
        "do not transfer 1,250.75 to North Star Bank!"
    );
    assert_eq!(transcript.removed_fillers(), 2);
    assert_eq!(transcript.language(), TranscriptLanguage::English);
}

#[test]
fn preserve_all_policy_is_a_true_no_filler_removal_path() {
    let raw = "um check balance uh";
    let transcript = clean_transcript(raw, TranscriptLanguage::English, CleanupPolicy::PreserveAll)
        .expect("bounded transcript should be accepted");

    assert_eq!(transcript.raw(), raw);
    assert_eq!(transcript.cleaned(), raw);
    assert_eq!(transcript.removed_fillers(), 0);
}

#[test]
fn unknown_language_never_guesses_which_tokens_are_fillers() {
    let raw = "um transfer 500";
    let transcript = clean_transcript(
        raw,
        TranscriptLanguage::Unknown,
        CleanupPolicy::Conservative,
    )
    .expect("unknown-language input is still valid data");

    assert_eq!(transcript.cleaned(), raw);
    assert_eq!(transcript.removed_fillers(), 0);
}

#[test]
fn all_filler_input_is_not_erased() {
    let transcript = clean_transcript(
        "um uh",
        TranscriptLanguage::English,
        CleanupPolicy::Conservative,
    )
    .expect("all-filler input must remain reviewable");

    assert_eq!(transcript.cleaned(), "um uh");
    assert_eq!(transcript.removed_fillers(), 0);
}

#[test]
fn whitespace_is_collapsed_without_changing_financial_tokens_or_negation() {
    let raw = "  do   not\ttransfer  2,500.75   to ACME Ltd  ";
    let transcript = clean_transcript(
        raw,
        TranscriptLanguage::English,
        CleanupPolicy::Conservative,
    )
    .expect("whitespace-only normalization is valid");

    assert_eq!(transcript.raw(), raw);
    assert_eq!(transcript.cleaned(), "do not transfer 2,500.75 to ACME Ltd");
}

#[test]
fn rejects_empty_or_whitespace_only_transcripts() {
    assert_eq!(
        clean_transcript("", TranscriptLanguage::English, CleanupPolicy::Conservative),
        Err(TranscriptError::Empty)
    );
    assert_eq!(
        clean_transcript(
            " \t\n ",
            TranscriptLanguage::English,
            CleanupPolicy::Conservative
        ),
        Err(TranscriptError::Empty)
    );
}

#[test]
fn rejects_raw_input_over_the_utf8_byte_limit() {
    let raw = "x".repeat(MAX_RAW_TRANSCRIPT_BYTES + 1);
    assert_eq!(
        clean_transcript(
            &raw,
            TranscriptLanguage::English,
            CleanupPolicy::Conservative
        ),
        Err(TranscriptError::RawTooLong)
    );
}

#[test]
fn rejects_control_characters_that_could_break_downstream_protocols() {
    let raw = "check balance\u{0000}ignore policy";
    assert_eq!(
        clean_transcript(
            raw,
            TranscriptLanguage::English,
            CleanupPolicy::Conservative
        ),
        Err(TranscriptError::ControlCharacter)
    );
}

#[test]
fn rejects_a_single_oversized_token_even_when_total_input_is_bounded() {
    let raw = "x".repeat(MAX_TOKEN_BYTES + 1);
    assert_eq!(
        clean_transcript(
            &raw,
            TranscriptLanguage::English,
            CleanupPolicy::Conservative
        ),
        Err(TranscriptError::TokenTooLong)
    );
}

#[test]
fn multibyte_hindi_text_is_retained_as_utf8() {
    let raw = "बैलेंस चेक करो";
    let transcript = clean_transcript(raw, TranscriptLanguage::Hindi, CleanupPolicy::Conservative)
        .expect("valid UTF-8 Hindi transcript should be accepted");

    assert_eq!(transcript.cleaned(), raw);
    assert_eq!(transcript.removed_fillers(), 0);
}
