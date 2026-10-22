#[test]
fn linked_c_core_abi_matches_generated_headers() {
    let info = fintally_voice::ffi::verify_native_abi()
        .expect("the linked FinTally Voice C core should match its public headers");

    assert_eq!(info.feature_dimension, 32);
    assert_eq!(info.sample_rate_hz, 16_000);
    assert_eq!(info.frame_samples, 320);
    assert_eq!(info.hop_samples, 160);
}
