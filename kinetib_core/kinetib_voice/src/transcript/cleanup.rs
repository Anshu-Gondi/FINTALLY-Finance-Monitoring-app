//! Conservative, bounded transcript cleanup. Raw ASR output is never overwritten.

use thiserror::Error;

pub const MAX_RAW_TRANSCRIPT_BYTES: usize = 2048;
pub const MAX_CLEAN_TRANSCRIPT_BYTES: usize = 2048;
pub const MAX_FILLER_TOKENS: usize = 8;
pub const MAX_TOKEN_BYTES: usize = 64;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TranscriptLanguage {
    English,
    French,
    Hindi,
    Unknown,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CleanupPolicy {
    Conservative,
    PreserveAll,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Transcript {
    raw: String,
    cleaned: String,
    language: TranscriptLanguage,
    removed_fillers: u8,
}
impl Transcript {
    pub fn raw(&self) -> &str {
        &self.raw
    }
    pub fn cleaned(&self) -> &str {
        &self.cleaned
    }
    pub fn language(&self) -> TranscriptLanguage {
        self.language
    }
    pub fn removed_fillers(&self) -> u8 {
        self.removed_fillers
    }
}

#[derive(Debug, Error, Clone, PartialEq, Eq)]
pub enum TranscriptError {
    #[error("raw transcript is empty")]
    Empty,
    #[error("raw transcript exceeds the 2048-byte limit")]
    RawTooLong,
    #[error("cleaned transcript exceeds the 2048-byte limit")]
    CleanedTooLong,
    #[error("transcript contains a forbidden control character")]
    ControlCharacter,
    #[error("token exceeds the 64-byte limit")]
    TokenTooLong,
}

const EN_FILLERS: &[&str] = &["um", "uh", "erm", "er", "uhh"];
const FR_FILLERS: &[&str] = &["euh", "heu", "hmm"];
const HI_FILLERS: &[&str] = &["umm", "hmm", "अं", "उम्"];

/// Conservative cleanup for an ASR/service worker, not a hard real-time callback.
/// Only standalone known fillers are removed. Unknown language preserves tokens.
pub fn clean_transcript(
    raw: &str,
    language: TranscriptLanguage,
    policy: CleanupPolicy,
) -> Result<Transcript, TranscriptError> {
    if raw.is_empty() {
        return Err(TranscriptError::Empty);
    }
    if raw.len() > MAX_RAW_TRANSCRIPT_BYTES {
        return Err(TranscriptError::RawTooLong);
    }
    if raw
        .chars()
        .any(|c| c.is_control() && c != '\t' && c != '\n')
    {
        return Err(TranscriptError::ControlCharacter);
    }
    let normalized = raw.split_whitespace().collect::<Vec<_>>().join(" ");
    if normalized.is_empty() {
        return Err(TranscriptError::Empty);
    }

    let mut cleaned = String::with_capacity(normalized.len());
    let mut removed = 0u8;
    for token in normalized.split(' ') {
        if token.len() > MAX_TOKEN_BYTES {
            return Err(TranscriptError::TokenTooLong);
        }
        let candidate =
            token.trim_matches(|c: char| matches!(c, ',' | '.' | '!' | '?' | ':' | ';'));
        let filler = policy == CleanupPolicy::Conservative
            && removed < MAX_FILLER_TOKENS as u8
            && is_filler(candidate, language);
        if filler {
            removed += 1;
            continue;
        }
        if !cleaned.is_empty() {
            cleaned.push(' ');
        }
        cleaned.push_str(token);
    }
    // Never erase a transcript just because every token matched a filler list.
    if cleaned.is_empty() {
        cleaned = normalized;
        removed = 0;
    }
    if cleaned.len() > MAX_CLEAN_TRANSCRIPT_BYTES {
        return Err(TranscriptError::CleanedTooLong);
    }
    Ok(Transcript {
        raw: raw.to_owned(),
        cleaned,
        language,
        removed_fillers: removed,
    })
}

fn is_filler(token: &str, language: TranscriptLanguage) -> bool {
    match language {
        TranscriptLanguage::English => EN_FILLERS.iter().any(|f| token.eq_ignore_ascii_case(f)),
        TranscriptLanguage::French => FR_FILLERS.iter().any(|f| token.eq_ignore_ascii_case(f)),
        TranscriptLanguage::Hindi => HI_FILLERS.contains(&token),
        TranscriptLanguage::Unknown => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn preserves_raw_and_removes_standalone_fillers() {
        let t = clean_transcript(
            "Um transfer uh 250 to Acme",
            TranscriptLanguage::English,
            CleanupPolicy::Conservative,
        )
        .unwrap();
        assert_eq!(t.raw(), "Um transfer uh 250 to Acme");
        assert_eq!(t.cleaned(), "transfer 250 to Acme");
        assert_eq!(t.removed_fillers(), 2);
    }
    #[test]
    fn preserves_financial_values_payees_and_negation() {
        let raw = "um do not transfer 1,250.50 to ACME Ltd";
        let t = clean_transcript(
            raw,
            TranscriptLanguage::English,
            CleanupPolicy::Conservative,
        )
        .unwrap();
        assert_eq!(t.cleaned(), "do not transfer 1,250.50 to ACME Ltd");
    }
    #[test]
    fn unknown_language_preserves_all_tokens() {
        let t = clean_transcript(
            "um transfer 500",
            TranscriptLanguage::Unknown,
            CleanupPolicy::Conservative,
        )
        .unwrap();
        assert_eq!(t.cleaned(), "um transfer 500");
    }
    #[test]
    fn preserve_all_policy_keeps_fillers() {
        let t = clean_transcript(
            "um pay 10",
            TranscriptLanguage::English,
            CleanupPolicy::PreserveAll,
        )
        .unwrap();
        assert_eq!(t.cleaned(), "um pay 10");
    }
    #[test]
    fn all_filler_input_is_not_erased() {
        let t = clean_transcript(
            "um uh",
            TranscriptLanguage::English,
            CleanupPolicy::Conservative,
        )
        .unwrap();
        assert_eq!(t.cleaned(), "um uh");
        assert_eq!(t.removed_fillers(), 0);
    }
    #[test]
    fn enforces_raw_byte_limit() {
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
}
