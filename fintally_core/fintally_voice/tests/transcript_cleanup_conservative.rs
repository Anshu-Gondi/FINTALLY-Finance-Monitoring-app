use fintally_voice::transcript::{clean_transcript, CleanupPolicy, TranscriptLanguage};

#[test]
fn financial_words_and_values_survive_cleanup() {
    let raw = "uh do not send 2,500.75 to North Star Bank";
    let t = clean_transcript(
        raw,
        TranscriptLanguage::English,
        CleanupPolicy::Conservative,
    )
    .unwrap();
    assert_eq!(t.raw(), raw);
    assert_eq!(t.cleaned(), "do not send 2,500.75 to North Star Bank");
}
