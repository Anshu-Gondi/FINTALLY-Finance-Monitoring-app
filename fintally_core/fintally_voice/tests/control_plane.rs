//! Bounded worker/UI handoff tests: backpressure, shutdown, and failure recovery.
//!
//! These tests exercise control-plane queues only; they do not run in the native
//! audio callback and do not claim these queues belong to the 100 KiB DSP budget.

use fintally_voice::threads::control_llm::{
    control_llm_channel, LlmBackendError, LlmCompletion, LlmRequest, LlmRequestId,
    LlmRequestValidationError, LlmSubmitError, MAX_LLM_OUTPUT_BYTES, MAX_LLM_PROMPT_BYTES,
    MAX_RAW_TRANSCRIPT_BYTES,
};
use fintally_voice::threads::ui_bridge::{
    ui_bridge_channel, UiCommand, UiCommandPoll, UiCommandSendError, UiEvent, UiEventKind,
    UiEventPoll, UiEventPublishError, UiRuntimeState, UiText, UiTextError, MAX_UI_TEXT_BYTES,
};

fn request(id: u64) -> LlmRequest {
    LlmRequest::new(
        LlmRequestId(id),
        "transfer 250 to Rahul",
        "Generate display-only confirmation wording.",
    )
    .expect("test request should be valid")
}

#[test]
fn request_validation_rejects_empty_and_oversized_payloads() {
    assert!(matches!(
        LlmRequest::new(LlmRequestId(1), "", "prompt"),
        Err(LlmRequestValidationError::EmptyTranscript)
    ));
    assert!(matches!(
        LlmRequest::new(LlmRequestId(1), "transcript", "  "),
        Err(LlmRequestValidationError::EmptyPrompt)
    ));
    assert!(matches!(
        LlmRequest::new(
            LlmRequestId(1),
            "x".repeat(MAX_RAW_TRANSCRIPT_BYTES + 1),
            "prompt"
        ),
        Err(LlmRequestValidationError::TranscriptTooLarge { .. })
    ));
    assert!(matches!(
        LlmRequest::new(
            LlmRequestId(1),
            "transcript",
            "x".repeat(MAX_LLM_PROMPT_BYTES + 1)
        ),
        Err(LlmRequestValidationError::PromptTooLarge { .. })
    ));
}

#[test]
fn bounded_llm_queue_returns_the_original_request_on_backpressure() {
    let (mut sender, _worker) = control_llm_channel(1).expect("capacity one is valid");
    sender
        .try_submit(request(10))
        .expect("first request should enqueue");

    match sender.try_submit(request(11)) {
        Err(LlmSubmitError::QueueFull(returned)) => {
            assert_eq!(returned.id(), LlmRequestId(11));
            assert_eq!(returned.raw_transcript(), "transfer 250 to Rahul");
        }
        Err(other) => panic!("unexpected submit error: {other:?}"),
        Ok(()) => panic!("second request must not exceed queue capacity"),
    }
    assert_eq!(sender.stats().queue_full, 1);
    assert_eq!(sender.capacity(), 1);
}

#[test]
fn closed_llm_worker_returns_the_unqueued_request() {
    let (mut sender, worker) = control_llm_channel(1).unwrap();
    drop(worker);

    match sender.try_submit(request(77)) {
        Err(LlmSubmitError::WorkerClosed(returned)) => {
            assert_eq!(returned.id(), LlmRequestId(77));
        }
        Err(other) => panic!("unexpected submit error: {other:?}"),
        Ok(()) => panic!("submission to a closed worker must fail"),
    }
    assert_eq!(sender.stats().worker_closed, 1);
}

#[test]
fn worker_drains_queued_requests_then_exits_when_sender_is_dropped() {
    let (mut sender, mut worker) = control_llm_channel(2).expect("capacity two is valid");
    sender.try_submit(request(1)).unwrap();
    sender.try_submit(request(2)).unwrap();
    drop(sender);

    let mut delivered: Vec<(u64, Result<String, LlmBackendError>)> = Vec::new();
    let mut processor = |req: &LlmRequest| Ok(format!("mock result for {}", req.id().0));
    let mut deliver = |completion: LlmCompletion| {
        delivered.push((completion.id().0, completion.result().map(str::to_owned)));
        Ok(())
    };

    worker
        .run(&mut processor, &mut deliver)
        .expect("worker should finish cleanly after channel closure");
    drop(deliver);

    assert_eq!(delivered.len(), 2);
    assert_eq!(delivered[0].0, 1);
    assert_eq!(delivered[0].1.as_deref(), Ok("mock result for 1"));
    assert_eq!(delivered[1].0, 2);
    assert_eq!(delivered[1].1.as_deref(), Ok("mock result for 2"));
    assert_eq!(worker.stats().requests_received, 2);
    assert_eq!(worker.stats().requests_completed, 2);
}

#[test]
fn delivery_backpressure_returns_the_same_completion_for_retry() {
    let (mut sender, mut worker) = control_llm_channel(1).unwrap();
    sender.try_submit(request(42)).unwrap();
    drop(sender);

    let mut processor = |_req: &LlmRequest| Ok("bounded result".to_owned());
    let result = worker.run(&mut processor, &mut |completion| Err(completion));
    let completion = result.expect_err("delivery refusal must return the completion");

    assert_eq!(completion.id(), LlmRequestId(42));
    assert_eq!(completion.result(), Ok("bounded result"));
    assert_eq!(worker.stats().delivery_backpressure, 1);
    // A caller can retry delivery with the returned completion instead of losing it.
}

#[test]
fn backend_failure_is_reported_and_does_not_block_worker_shutdown() {
    let (mut sender, mut worker) = control_llm_channel(1).unwrap();
    sender.try_submit(request(7)).unwrap();
    drop(sender);

    let mut processor = |_req: &LlmRequest| Err(LlmBackendError::Timeout);
    let mut observed = None;
    let mut deliver = |completion: LlmCompletion| {
        observed = Some((completion.id(), completion.result().map(str::to_owned)));
        Ok(())
    };
    worker.run(&mut processor, &mut deliver).unwrap();
    drop(deliver);

    assert_eq!(
        observed,
        Some((LlmRequestId(7), Err(LlmBackendError::Timeout)))
    );
    assert_eq!(worker.stats().backend_failures, 1);
    assert_eq!(worker.stats().requests_completed, 1);
}

#[test]
fn oversized_model_output_is_rejected_not_truncated() {
    let (mut sender, mut worker) = control_llm_channel(1).unwrap();
    sender.try_submit(request(8)).unwrap();
    drop(sender);

    let mut processor = |_req: &LlmRequest| Ok("z".repeat(MAX_LLM_OUTPUT_BYTES + 1));
    let mut observed = None;
    let mut deliver = |completion: LlmCompletion| {
        observed = Some(completion.result().map(str::to_owned));
        Ok(())
    };
    worker.run(&mut processor, &mut deliver).unwrap();
    drop(deliver);

    assert!(matches!(
        observed,
        Some(Err(LlmBackendError::OutputTooLarge { .. }))
    ));
    assert_eq!(worker.stats().backend_failures, 1);
}

#[test]
fn ui_command_queue_is_bounded_and_preserves_the_rejected_command() {
    let (mut runtime, mut client) = ui_bridge_channel(1, 1).unwrap();
    let first = UiCommand::StartListening { session_id: 5 };
    let second = UiCommand::StopListening { session_id: 5 };

    client.try_send_command(first).unwrap();
    assert_eq!(
        client.try_send_command(second),
        Err(UiCommandSendError::QueueFull(second))
    );
    assert_eq!(runtime.try_recv_command(), UiCommandPoll::Command(first));
    assert_eq!(client.stats().command_queue_full, 1);
}

#[test]
fn draft_approval_is_a_separate_explicit_ui_command() {
    let (mut runtime, mut client) = ui_bridge_channel(2, 1).unwrap();
    let approval = UiCommand::ApproveDraft { draft_id: 314 };
    client
        .try_send_command(approval)
        .expect("explicit user approval command should enqueue");
    assert_eq!(runtime.try_recv_command(), UiCommandPoll::Command(approval));
}

#[test]
fn draft_rejection_is_a_distinct_explicit_ui_command() {
    let (mut runtime, mut client) = ui_bridge_channel(2, 1).unwrap();
    let rejection = UiCommand::RejectDraft { draft_id: 315 };
    client
        .try_send_command(rejection)
        .expect("explicit user rejection command should enqueue");
    assert_eq!(
        runtime.try_recv_command(),
        UiCommandPoll::Command(rejection)
    );
}

#[test]
fn final_transcript_event_is_returned_intact_when_ui_is_backpressured() {
    let (mut runtime, mut client) = ui_bridge_channel(1, 1).unwrap();
    runtime
        .try_publish_event(UiEvent::new(
            None,
            UiEventKind::StateChanged {
                state: UiRuntimeState::Listening,
            },
        ))
        .unwrap();

    let final_event = UiEvent::new(
        Some(99),
        UiEventKind::FinalTranscript {
            text: UiText::new("transfer 250 to Rahul").unwrap(),
        },
    );
    match runtime.try_publish_event(final_event) {
        Err(UiEventPublishError::QueueFull(returned)) => {
            assert_eq!(returned.session_id, Some(99));
            match returned.kind {
                UiEventKind::FinalTranscript { text } => {
                    assert_eq!(text.as_str(), "transfer 250 to Rahul");
                }
                other => panic!("final transcript changed event kind: {other:?}"),
            }
        }
        Err(other) => panic!("unexpected event publication error: {other:?}"),
        Ok(()) => panic!("full UI queue must reject rather than hide the event"),
    }
    assert!(matches!(client.try_recv_event(), UiEventPoll::Event(_)));
    assert_eq!(runtime.stats().event_queue_full, 1);
}

#[test]
fn closed_ui_receiver_returns_the_original_command() {
    let (runtime, mut client) = ui_bridge_channel(1, 1).unwrap();
    drop(runtime);
    let command = UiCommand::ResetSession { session_id: 123 };

    assert_eq!(
        client.try_send_command(command),
        Err(UiCommandSendError::RuntimeClosed(command))
    );
}

#[test]
fn ui_text_has_a_hard_utf8_byte_cap_and_debug_hides_contents() {
    assert!(matches!(
        UiText::new("x".repeat(MAX_UI_TEXT_BYTES + 1)),
        Err(UiTextError::TooLarge { .. })
    ));
    let text = UiText::new("private balance 98765").unwrap();
    let debug = format!("{text:?}");
    assert!(debug.contains("bytes"));
    assert!(!debug.contains("98765"));
}
