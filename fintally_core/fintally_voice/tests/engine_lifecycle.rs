//! Native engine integration tests: ABI contract, memory guardrails, lifecycle,
//! frame-size validation, bounded events, and text-builder behavior.

use fintally_voice::engine::{EngineConfig, EngineError, EngineState, VoiceEngine, VoiceLanguage};

const HARD_DSP_BUDGET_BYTES: usize = 100 * 1024;
const FRAME_SAMPLES: usize = 320;

#[test]
fn default_engine_respects_the_accounted_dsp_working_set_budget() {
    let engine = VoiceEngine::new(EngineConfig::default())
        .expect("native core and codebook ABI should initialize");
    let abi = engine.native_abi();
    let report = engine.memory_report();

    assert_eq!(abi.feature_dimension, 32);
    assert_eq!(abi.sample_rate_hz, 16_000);
    assert_eq!(abi.frame_samples as usize, FRAME_SAMPLES);
    assert_eq!(abi.hop_samples as usize, 160);
    assert!(report.total_accounted_bytes <= report.budget_bytes);
    assert!(report.budget_bytes <= HARD_DSP_BUDGET_BYTES);
    assert_eq!(report.alignment_slack_bytes, 4 * (64 - 1));
    assert_eq!(
        report.total_accounted_bytes,
        report.dsp_workspace_bytes
            + report.pipeline_workspace_bytes
            + report.event_storage_bytes
            + report.text_storage_bytes
            + report.native_context_bytes
            + report.active_codebook_bytes
            + report.one_frame_transient_bytes
            + report.alignment_slack_bytes
    );
}

#[test]
fn invalid_resource_configuration_fails_before_native_allocation() {
    let config = EngineConfig {
        event_capacity: 1,
        ..EngineConfig::default()
    };
    assert!(matches!(
        VoiceEngine::new(config),
        Err(EngineError::InvalidConfiguration(_))
    ));

    let config = EngineConfig {
        working_set_budget_bytes: HARD_DSP_BUDGET_BYTES + 1,
        ..EngineConfig::default()
    };
    assert!(matches!(
        VoiceEngine::new(config),
        Err(EngineError::InvalidConfiguration(_))
    ));
}

#[test]
fn lifecycle_rejects_illegal_transitions_and_terminal_restarts() {
    let mut engine = VoiceEngine::new(EngineConfig::default()).unwrap();
    assert_eq!(engine.state(), EngineState::Initialized);

    engine.start().unwrap();
    assert!(matches!(
        engine.start(),
        Err(EngineError::InvalidState { .. })
    ));
    engine.stop().unwrap();
    assert_eq!(engine.state(), EngineState::Initialized);

    engine.shutdown();
    assert_eq!(engine.state(), EngineState::Stopped);
    assert!(matches!(
        engine.start(),
        Err(EngineError::InvalidState { .. })
    ));
    assert!(matches!(
        engine.select_language("en"),
        Err(EngineError::InvalidState { .. })
    ));
    assert!(matches!(
        engine.active_language(),
        Err(EngineError::InvalidState { .. })
    ));
    assert!(matches!(
        engine.append_recognized_text("check balance"),
        Err(EngineError::InvalidState { .. })
    ));
    assert!(matches!(
        engine.try_pop_event(),
        Err(EngineError::InvalidState { .. })
    ));
}

#[test]
fn frame_processing_requires_running_state_and_exact_frame_size() {
    let mut engine = VoiceEngine::new(EngineConfig::default()).unwrap();
    let frame = [0_i16; FRAME_SAMPLES];
    assert!(matches!(
        engine.process_pcm16_frame(&frame, 0, 0),
        Err(EngineError::InvalidState { .. })
    ));

    engine.select_language("en").unwrap();
    assert_eq!(engine.active_language().unwrap(), VoiceLanguage::English);
    engine.start().unwrap();
    let short_frame = [0_i16; FRAME_SAMPLES - 1];
    match engine.process_pcm16_frame(&short_frame, 0, 0) {
        Err(EngineError::InvalidFrameLength { expected, actual }) => {
            assert_eq!(expected, FRAME_SAMPLES);
            assert_eq!(actual, FRAME_SAMPLES - 1);
        }
        other => panic!("expected a frame-length error, got {other:?}"),
    }

    let summary = engine
        .process_pcm16_frame(&frame, 10_000_000, 160)
        .expect("a valid mono PCM16 frame should pass through the native DSP/VQ pipeline");
    assert!(summary.best_distance_squared.is_finite());
    assert!(summary.second_best_distance_squared.is_finite());
    assert!(summary.relative_gap.is_finite());
    assert_eq!(engine.stats().frames_processed, 1);
    assert_eq!(engine.stats().frames_published, 1);

    let mut popped = 0usize;
    while engine.try_pop_event().unwrap().is_some() {
        popped += 1;
    }
    assert!(
        popped > 0,
        "a successfully processed frame should publish events"
    );
    engine.stop().unwrap();
}

#[test]
fn recognized_text_round_trips_through_bounded_native_builder() {
    let mut engine = VoiceEngine::new(EngineConfig::default()).unwrap();
    engine.append_recognized_text("check balance").unwrap();

    let mut output = [0_u8; 128];
    let length = engine
        .flush_recognized_text(&mut output)
        .expect("output storage is large enough");
    let text = std::str::from_utf8(&output[..length]).expect("native builder returns UTF-8");
    assert_eq!(text, "check balance");

    assert!(matches!(
        engine.flush_recognized_text(&mut []),
        Err(EngineError::InvalidConfiguration(_))
    ));
}
