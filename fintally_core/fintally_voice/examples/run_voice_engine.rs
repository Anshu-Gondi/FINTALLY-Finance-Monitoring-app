//! Runnable smoke harness for the FinTally Voice native engine and review path.
//!
//! Usage from the `fintally_core` workspace:
//!   cargo run -p fintally_voice --example run_voice_engine -- "transfer 250 to Rahul"
//!
//! Without arguments it uses "check balance". The PCM is a deterministic test
//! tone, not microphone audio. The transcript argument stands in for an ASR
//! result. The LLM processor below is an explicit mock; replace that closure
//! with your bounded local backend adapter for a real model integration.

use std::error::Error;
use std::f32::consts::TAU;

use fintally_voice::engine::{EngineConfig, EngineEventKind, VoiceEngine};
use fintally_voice::intent::{classify_intent, IntentResult};
use fintally_voice::prompt::{build_prompt_draft, DraftMode, PromptDraft};
use fintally_voice::threads::control_llm::{
    control_llm_channel, LlmBackendError, LlmCompletion, LlmRequest, LlmRequestId,
};
use fintally_voice::threads::ui_bridge::{
    default_ui_bridge_channel, UiEvent, UiEventKind, UiEventPoll, UiText,
};
use fintally_voice::transcript::{clean_transcript, CleanupPolicy, Transcript, TranscriptLanguage};

const SAMPLE_RATE_HZ: usize = 16_000;
const FRAME_SAMPLES: usize = 320;
const HOP_SAMPLES: usize = 160;
const MOCK_REQUEST_ID: u64 = 1;
const MOCK_DRAFT_ID: u64 = 1;

fn main() -> Result<(), Box<dyn Error>> {
    let raw_transcript = std::env::args().skip(1).collect::<Vec<_>>().join(" ");
    let raw_transcript = if raw_transcript.trim().is_empty() {
        "check balance".to_owned()
    } else {
        raw_transcript
    };

    println!("FinTally Voice smoke harness");
    exercise_native_engine()?;

    let transcript = clean_transcript(
        &raw_transcript,
        TranscriptLanguage::English,
        CleanupPolicy::Conservative,
    )?;
    let intent = classify_intent(transcript.cleaned(), transcript.language())?;
    let draft = build_prompt_draft(&transcript, &intent)?;
    print_decision(&transcript, &intent, &draft);

    let model_output = run_mock_llm_handoff(&transcript, &draft)?;
    let (display_text, model_reply_validated) = match draft.parse_model_output(&model_output) {
        Ok(suggestion) => {
            let mut display = suggestion.message().to_owned();
            if let Some(question) = suggestion.question() {
                display.push(' ');
                display.push_str(question);
            }
            (display, true)
        }
        Err(error) => {
            eprintln!("Mock/backend output rejected ({error}); using fixed fallback.");
            (draft.deterministic_fallback().to_owned(), false)
        }
    };

    publish_demo_ui_event(display_text.as_str())?;
    println!("Model-shaped output passed strict validation: {model_reply_validated}");
    println!("Smoke harness completed. No finance operation was executed.");
    Ok(())
}

fn exercise_native_engine() -> Result<(), Box<dyn Error>> {
    let mut engine = VoiceEngine::new(EngineConfig::default())?;
    let abi = engine.native_abi();
    let memory = engine.memory_report();

    if abi.sample_rate_hz as usize != SAMPLE_RATE_HZ
        || abi.frame_samples as usize != FRAME_SAMPLES
        || abi.hop_samples as usize != HOP_SAMPLES
        || abi.feature_dimension != 32
    {
        return Err("native ABI does not match the 16 kHz / 320 / 160 / 32 contract".into());
    }
    if memory.total_accounted_bytes > memory.budget_bytes || memory.budget_bytes > 100 * 1024 {
        return Err("native DSP working-set accounting exceeds the configured hard budget".into());
    }

    engine.select_language("en")?;
    engine.start()?;

    let mut frame = [0_i16; FRAME_SAMPLES];
    let mut event_count = 0usize;
    // 8 frames cover 80 ms of frame-hop time. Reuse the stack buffer every time.
    for frame_index in 0..8_u64 {
        let frame_start = frame_index * HOP_SAMPLES as u64;
        for (offset, sample) in frame.iter_mut().enumerate() {
            let absolute_sample = frame_start + offset as u64;
            let phase = TAU * 220.0 * (absolute_sample as f32 / SAMPLE_RATE_HZ as f32);
            *sample = (phase.sin() * 8_000.0) as i16;
        }

        let summary = engine.process_pcm16_frame(&frame, frame_index * 10_000_000, frame_start)?;
        if !summary.best_distance_squared.is_finite()
            || !summary.second_best_distance_squared.is_finite()
            || !summary.relative_gap.is_finite()
        {
            return Err("native quantizer returned non-finite output".into());
        }
        while let Some(event) = engine.try_pop_event()? {
            event_count += 1;
            match event.kind {
                EngineEventKind::FeaturesReady { dimension, .. } if dimension != 32 => {
                    return Err("native feature event has an unexpected dimension".into());
                }
                EngineEventKind::TextReady { text, .. } => {
                    // Do not print financial text from an audio/event path by default.
                    println!("Native text event received ({} bytes)", text.len());
                }
                _ => {}
            }
        }
    }

    engine.stop()?;
    engine.reset()?;
    engine.shutdown();
    println!(
        "Native smoke: ABI OK; accounted DSP bytes {}/{}; events observed {}.",
        memory.total_accounted_bytes, memory.budget_bytes, event_count
    );
    Ok(())
}

fn print_decision(transcript: &Transcript, intent: &IntentResult, draft: &PromptDraft) {
    // Development diagnostics only. Resolve slots against the exact
    // cleaned transcript from which the classifier created their spans.
    let source = transcript.cleaned();

    let amount = intent.slots.amount.and_then(|span| span.resolve(source));

    let payee = intent.slots.payee.and_then(|span| span.resolve(source));

    let source_account = intent
        .slots
        .source_account
        .and_then(|span| span.resolve(source));

    let category = intent.slots.category.and_then(|span| span.resolve(source));

    let date = intent.slots.date.and_then(|span| span.resolve(source));

    let period = intent.slots.period.and_then(|span| span.resolve(source));

    let interest_rate = intent
        .slots
        .interest_rate
        .and_then(|span| span.resolve(source));

    let tenure = intent.slots.tenure.and_then(|span| span.resolve(source));

    println!(
        "Transcript bytes: raw={}, cleaned={}",
        transcript.raw().len(),
        source.len()
    );

    println!(
        "Intent: {:?}; decision: {:?}; confidence score: {:.3}",
        intent.kind, intent.decision, intent.confidence
    );

    // Development-only diagnostics; do not log financial data in production.
    println!("Extracted slots (development only):");
    println!("  amount:         {amount:?}");
    println!("  payee:          {payee:?}");
    println!("  source account: {source_account:?}");
    println!("  category:       {category:?}");
    println!("  date:            {date:?}");
    println!("  period:          {period:?}");
    println!("  interest rate:   {interest_rate:?}");
    println!("  tenure:          {tenure:?}");
    println!("  currency:        {:?}", intent.slots.currency);

    println!(
        "Prompt mode: {:?}; prompt bytes: {}",
        draft.mode(),
        draft.prompt().len()
    );

    println!("Deterministic fallback available: yes");

    if draft.mode() == DraftMode::Confirmation {
        println!(
            "Review required: display structured slot values and wait for explicit user approval."
        );
    }
}

fn run_mock_llm_handoff(
    transcript: &Transcript,
    draft: &PromptDraft,
) -> Result<String, Box<dyn Error>> {
    let (mut sender, mut worker) = control_llm_channel(1)?;
    sender.try_submit(LlmRequest::new(
        LlmRequestId(MOCK_REQUEST_ID),
        transcript.raw(),
        draft.prompt(),
    )?)?;
    // Dropping the last sender makes worker shutdown deterministic after queued work drains.
    drop(sender);

    let mode = draft.mode();
    let mut output: Option<Result<String, LlmBackendError>> = None;
    let mut processor = move |_request: &LlmRequest| Ok(mock_model_json(mode).to_owned());
    let mut deliver = |completion: LlmCompletion| {
        output = Some(completion.result().map(str::to_owned));
        Ok(())
    };
    worker
        .run(&mut processor, &mut deliver)
        .map_err(|_completion| std::io::Error::other("mock completion delivery was refused"))?;
    drop(deliver);

    match output.unwrap_or(Err(LlmBackendError::Unavailable)) {
        Ok(output) => Ok(output),
        Err(_error) => Err(std::io::Error::other("mock LLM backend failed").into()),
    }
}

fn mock_model_json(mode: DraftMode) -> &'static str {
    match mode {
        DraftMode::Confirmation => {
            r#"{"status":"confirm","message":"Please review the proposed transfer details.","question":"Do you confirm these details?","risk_note":null}"#
        }
        DraftMode::Clarification => {
            r#"{"status":"clarify","message":"The request needs one clarification.","question":"Could you clarify the intended request?","risk_note":null}"#
        }
        DraftMode::ReadOnlySummary => {
            r#"{"status":"summary","message":"This is a read-only request. Retrieve any account facts from FinTally storage.","question":null,"risk_note":null}"#
        }
    }
}

fn publish_demo_ui_event(display_text: &str) -> Result<(), Box<dyn Error>> {
    let (mut runtime, mut ui_client) = default_ui_bridge_channel();
    runtime.try_publish_event(UiEvent::new(
        Some(1),
        UiEventKind::DraftReady {
            draft_id: MOCK_DRAFT_ID,
            text: UiText::new(display_text.to_owned())?,
        },
    ))?;

    match ui_client.try_recv_event() {
        UiEventPoll::Event(event) => match event.kind {
            UiEventKind::DraftReady { draft_id, text } => {
                println!("UI bridge delivered draft #{draft_id}: {}", text.as_str());
            }
            _ => return Err("UI bridge delivered an unexpected event type".into()),
        },
        UiEventPoll::Empty => return Err("expected UI event was not available".into()),
        UiEventPoll::Closed => return Err("UI bridge closed before delivering event".into()),
    }
    Ok(())
}
