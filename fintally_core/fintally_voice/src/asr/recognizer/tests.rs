//! Unit tests for the bounded ASR runtime and its test backend.

use super::*;
use crate::asr::backend::{AsrBackend, AsrError, BackendTranscript, TranscriptKind, UpdateSink};
use crate::asr::{ASR_FRAME_SAMPLES, MAX_TRANSCRIPT_BYTES};
use std::thread;
use std::time::Duration;

struct EchoBackend;

impl AsrBackend for EchoBackend {
    fn push_audio(
        &mut self,
        _pcm16: &[i16],
        _sample_rate_hz: u32,
        sink: &mut UpdateSink<'_>,
    ) -> Result<(), AsrError> {
        sink(BackendTranscript {
            text: "test transcript",
            kind: TranscriptKind::Partial,
        })
    }

    fn cancel(&mut self) -> Result<(), AsrError> {
        Ok(())
    }

    fn finish(&mut self, sink: &mut UpdateSink<'_>) -> Result<(), AsrError> {
        sink(BackendTranscript {
            text: "test transcript",
            kind: TranscriptKind::Final,
        })
    }
}

#[test]
fn default_runtime_estimate_fits_budget() {
    let config = AsrRuntimeConfig::default();
    assert!(config.validate().is_ok());
    assert!(config.estimated_runtime_bytes() <= ASR_RUNTIME_BUDGET_BYTES);
    assert!(
        config
            .memory_report()
            .asr_model_weights_and_native_backend_excluded
    );
}

#[test]
fn rejects_wrong_frame_length_and_zero_session() {
    assert!(matches!(
        AsrAudioFrame::new(1, 0, 0, &[0; 10]),
        Err(AsrError::InvalidFrameLength { .. })
    ));
    assert!(matches!(
        AsrAudioFrame::new(0, 0, 0, &[0; ASR_FRAME_SAMPLES]),
        Err(AsrError::InvalidConfig(_))
    ));
}

#[test]
fn transcript_event_uses_fixed_payload_and_checks_capacity() {
    let short = AsrEvent::transcript(
        1,
        2,
        3,
        BackendTranscript {
            text: "hello",
            kind: TranscriptKind::Final,
        },
    );
    assert_eq!(short.text(), Some("hello"));

    let long_text = "x".repeat(MAX_TRANSCRIPT_BYTES + 1);
    let too_long = AsrEvent::transcript(
        1,
        2,
        3,
        BackendTranscript {
            text: &long_text,
            kind: TranscriptKind::Final,
        },
    );
    assert_eq!(too_long.kind, AsrEventKind::TranscriptTooLong);
    assert_eq!(too_long.text(), None);
}

#[test]
fn fake_backend_runs_only_in_worker_and_publishes_events() {
    let config = AsrRuntimeConfig::default();
    let mut runtime = AsrRuntime::spawn_with_factory(config, || {
        Ok::<Box<dyn AsrBackend>, AsrError>(Box::new(EchoBackend))
    })
    .expect("test backend should start");

    let frame = AsrAudioFrame::new(1, 100, 320, &[0; ASR_FRAME_SAMPLES]).expect("valid frame");
    runtime
        .try_submit_frame(frame)
        .expect("queue should accept frame");

    let mut found = false;
    for _ in 0..100 {
        if let Some(event) = runtime.try_recv_event() {
            if event.kind == AsrEventKind::PartialTranscript {
                assert_eq!(event.text(), Some("test transcript"));
                found = true;
                break;
            }
        }
        thread::sleep(Duration::from_millis(2));
    }
    assert!(
        found,
        "ASR worker should publish the fake backend's transcript"
    );

    runtime
        .request_shutdown()
        .expect("shutdown command should fit");
    let mut stopped = false;
    for _ in 0..100 {
        if runtime
            .try_recv_event()
            .is_some_and(|event| event.kind == AsrEventKind::WorkerStopped)
        {
            stopped = true;
            break;
        }
        thread::sleep(Duration::from_millis(2));
    }
    assert!(stopped, "ASR worker should publish WorkerStopped");
    runtime.join().expect("worker should shut down");
}
