//! Allocation-only probes for FinTally Voice.
//!
//! This executable is deliberately separate from voice_runtime.rs so the
//! counting global allocator cannot distort the latency benchmarks.

use std::alloc::{GlobalAlloc, Layout, System};
use std::env;
use std::fs;
use std::hint::black_box;
use std::io::{self, Write};
use std::path::Path;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};

use fintally_voice::engine::{EngineConfig, VoiceEngine};
use fintally_voice::intent::{classify_intent, MAX_INTENT_TEXT_BYTES};
use fintally_voice::prompt::build_prompt_draft;
use fintally_voice::transcript::{
    clean_transcript, CleanupPolicy, Transcript, TranscriptLanguage, MAX_RAW_TRANSCRIPT_BYTES,
};

const BENCH_FRAME_SAMPLES: usize = 320;

struct CountingAllocator;

static ALLOC_CALLS: AtomicU64 = AtomicU64::new(0);
static ALLOC_BYTES: AtomicU64 = AtomicU64::new(0);
static REALLOC_CALLS: AtomicU64 = AtomicU64::new(0);
static REALLOC_BYTES: AtomicU64 = AtomicU64::new(0);
static DEALLOC_CALLS: AtomicU64 = AtomicU64::new(0);
static LIVE_BYTES: AtomicUsize = AtomicUsize::new(0);
static PEAK_LIVE_BYTES: AtomicUsize = AtomicUsize::new(0);

#[global_allocator]
static GLOBAL: CountingAllocator = CountingAllocator;

unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // SAFETY: forward the requested layout to the system allocator.
        let ptr = unsafe { System.alloc(layout) };
        if !ptr.is_null() {
            ALLOC_CALLS.fetch_add(1, Ordering::Relaxed);
            ALLOC_BYTES.fetch_add(layout.size() as u64, Ordering::Relaxed);
            add_live(layout.size());
        }
        ptr
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        // SAFETY: forward the requested layout to the system allocator.
        let ptr = unsafe { System.alloc_zeroed(layout) };
        if !ptr.is_null() {
            ALLOC_CALLS.fetch_add(1, Ordering::Relaxed);
            ALLOC_BYTES.fetch_add(layout.size() as u64, Ordering::Relaxed);
            add_live(layout.size());
        }
        ptr
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        DEALLOC_CALLS.fetch_add(1, Ordering::Relaxed);
        LIVE_BYTES.fetch_sub(layout.size(), Ordering::Relaxed);
        // SAFETY: this pointer/layout pair came from the matching allocator.
        unsafe { System.dealloc(ptr, layout) };
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        // SAFETY: forward the original pointer/layout and requested size.
        let new_ptr = unsafe { System.realloc(ptr, layout, new_size) };
        if !new_ptr.is_null() {
            REALLOC_CALLS.fetch_add(1, Ordering::Relaxed);
            REALLOC_BYTES.fetch_add(new_size as u64, Ordering::Relaxed);
            if new_size >= layout.size() {
                add_live(new_size - layout.size());
            } else {
                LIVE_BYTES.fetch_sub(layout.size() - new_size, Ordering::Relaxed);
            }
        }
        new_ptr
    }
}

fn add_live(bytes: usize) {
    let live = LIVE_BYTES
        .fetch_add(bytes, Ordering::Relaxed)
        .saturating_add(bytes);
    let mut peak = PEAK_LIVE_BYTES.load(Ordering::Relaxed);
    while live > peak {
        match PEAK_LIVE_BYTES.compare_exchange_weak(
            peak,
            live,
            Ordering::Relaxed,
            Ordering::Relaxed,
        ) {
            Ok(_) => break,
            Err(observed) => peak = observed,
        }
    }
}

#[derive(Clone, Copy)]
struct Snapshot {
    alloc_calls: u64,
    alloc_bytes: u64,
    realloc_calls: u64,
    realloc_bytes: u64,
    dealloc_calls: u64,
    live_bytes: usize,
    peak_live_bytes: usize,
}

fn snapshot() -> Snapshot {
    Snapshot {
        alloc_calls: ALLOC_CALLS.load(Ordering::Relaxed),
        alloc_bytes: ALLOC_BYTES.load(Ordering::Relaxed),
        realloc_calls: REALLOC_CALLS.load(Ordering::Relaxed),
        realloc_bytes: REALLOC_BYTES.load(Ordering::Relaxed),
        dealloc_calls: DEALLOC_CALLS.load(Ordering::Relaxed),
        live_bytes: LIVE_BYTES.load(Ordering::Relaxed),
        peak_live_bytes: PEAK_LIVE_BYTES.load(Ordering::Relaxed),
    }
}

struct ResultRow {
    name: String,
    ops: u64,
    alloc_calls_per_op: f64,
    alloc_bytes_per_op: f64,
    realloc_calls_per_op: f64,
    dealloc_calls_per_op: f64,
    peak_additional_live_bytes: usize,
    live_delta_bytes_per_op: f64,
}

fn wants_case(name: &str) -> bool {
    match env::var("FINTALLY_BENCH_FILTER") {
        Ok(filter) if !filter.is_empty() => name.contains(&filter),
        _ => true,
    }
}

fn run_case<F>(name: &str, operations: usize, mut operation: F) -> ResultRow
where
    F: FnMut(),
{
    if !wants_case(name) {
        println!(
            "SKIP name={} filter={}",
            name,
            env::var("FINTALLY_BENCH_FILTER").unwrap_or_default()
        );
        return ResultRow {
            name: name.to_owned(),
            ops: 0,
            alloc_calls_per_op: 0.0,
            alloc_bytes_per_op: 0.0,
            realloc_calls_per_op: 0.0,
            dealloc_calls_per_op: 0.0,
            peak_additional_live_bytes: 0,
            live_delta_bytes_per_op: 0.0,
        };
    }

    for _ in 0..operations.min(1_000) {
        operation();
    }
    let before = snapshot();
    // Track peak above the existing live-allocation baseline for this case.
    PEAK_LIVE_BYTES.store(before.live_bytes, Ordering::Relaxed);
    for _ in 0..operations {
        operation();
    }
    let after = snapshot();

    let ops = operations.max(1) as u64;
    let alloc_calls = after.alloc_calls.saturating_sub(before.alloc_calls);
    let realloc_calls = after.realloc_calls.saturating_sub(before.realloc_calls);
    let alloc_bytes = after
        .alloc_bytes
        .saturating_sub(before.alloc_bytes)
        .saturating_add(after.realloc_bytes.saturating_sub(before.realloc_bytes));
    let peak_additional = after.peak_live_bytes.saturating_sub(before.live_bytes);
    let live_delta = after.live_bytes as i128 - before.live_bytes as i128;
    let row = ResultRow {
        name: name.to_owned(),
        ops,
        alloc_calls_per_op: alloc_calls as f64 / ops as f64,
        alloc_bytes_per_op: alloc_bytes as f64 / ops as f64,
        realloc_calls_per_op: realloc_calls as f64 / ops as f64,
        dealloc_calls_per_op: after.dealloc_calls.saturating_sub(before.dealloc_calls) as f64
            / ops as f64,
        peak_additional_live_bytes: peak_additional,
        live_delta_bytes_per_op: live_delta as f64 / ops as f64,
    };
    println!(
        "ALLOC name={} ops={} alloc_calls_op={:.4} requested_bytes_op={:.2} realloc_calls_op={:.4} dealloc_calls_op={:.4} peak_additional_live_bytes={} live_delta_bytes_op={:.3}",
        row.name,
        row.ops,
        row.alloc_calls_per_op,
        row.alloc_bytes_per_op,
        row.realloc_calls_per_op,
        row.dealloc_calls_per_op,
        row.peak_additional_live_bytes,
        row.live_delta_bytes_per_op,
    );
    row
}

fn make_bounded_text() -> String {
    let token = "unrecognized ";
    let mut text = String::with_capacity(MAX_RAW_TRANSCRIPT_BYTES);
    while text.len() + token.len() <= MAX_RAW_TRANSCRIPT_BYTES {
        text.push_str(token);
    }
    if text.len() < MAX_RAW_TRANSCRIPT_BYTES {
        text.push_str(&"x".repeat(MAX_RAW_TRANSCRIPT_BYTES - text.len()));
    }
    text
}

fn prepare_transcript(text: &str) -> Transcript {
    clean_transcript(
        text,
        TranscriptLanguage::English,
        CleanupPolicy::Conservative,
    )
    .expect("benchmark fixture must be valid")
}

fn parse_operations() -> usize {
    env::var("FINTALLY_ALLOC_OPS")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .map(|n| n.clamp(1, 1_000_000))
        .unwrap_or(10_000)
}

fn linux_status_value(key: &str) -> Option<String> {
    let status = fs::read_to_string("/proc/self/status").ok()?;
    status.lines().find_map(|line| {
        let (found, value) = line.split_once(':')?;
        (found == key).then(|| value.trim().to_owned())
    })
}

fn write_csv(path: &str, rows: &[ResultRow]) -> io::Result<()> {
    let output_path = Path::new(path);
    if let Some(parent) = output_path.parent() {
        if !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent)?;
        }
    }
    let mut file = fs::File::create(output_path)?;
    writeln!(file, "name,ops,alloc_calls_per_op,requested_bytes_per_op,realloc_calls_per_op,dealloc_calls_per_op,peak_additional_live_bytes,live_delta_bytes_per_op")?;
    for row in rows.iter().filter(|r| r.ops > 0) {
        writeln!(
            file,
            "{},{},{:.6},{:.3},{:.6},{:.6},{},{:.3}",
            row.name,
            row.ops,
            row.alloc_calls_per_op,
            row.alloc_bytes_per_op,
            row.realloc_calls_per_op,
            row.dealloc_calls_per_op,
            row.peak_additional_live_bytes,
            row.live_delta_bytes_per_op,
        )?;
    }
    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let operations = parse_operations();
    let mut rows = Vec::with_capacity(16);

    let balance = prepare_transcript("check balance");
    let transfer = prepare_transcript("transfer 250 to Rahul");
    let negated = prepare_transcript("do not transfer 250 to Rahul");
    let bounded_raw = make_bounded_text();
    let bounded = prepare_transcript(&bounded_raw);
    let invalid_control = "transfer 250\u{0000}to Rahul";
    let oversized_intent = "x".repeat(MAX_INTENT_TEXT_BYTES + 1);
    let balance_intent = classify_intent(balance.cleaned(), balance.language())?;
    let transfer_intent = classify_intent(transfer.cleaned(), transfer.language())?;
    let negated_intent = classify_intent(negated.cleaned(), negated.language())?;
    if !negated_intent.negated {
        return Err("negation allocation fixture was not classified as negated".into());
    }

    println!("FinTally Voice Rust allocation probe");
    println!(
        "CONFIG operations_per_case={} filter={}",
        operations,
        env::var("FINTALLY_BENCH_FILTER").unwrap_or_else(|_| "<all>".to_owned())
    );

    rows.push(run_case("cleanup/happy_balance", operations, || {
        black_box(prepare_transcript("check balance"));
    }));
    rows.push(run_case(
        "cleanup/worst_bounded_transcript",
        operations,
        || {
            black_box(
                clean_transcript(
                    &bounded_raw,
                    TranscriptLanguage::English,
                    CleanupPolicy::Conservative,
                )
                .expect("bounded fixture"),
            );
        },
    ));
    rows.push(run_case(
        "cleanup/failure_control_character",
        operations,
        || {
            black_box(
                clean_transcript(
                    invalid_control,
                    TranscriptLanguage::English,
                    CleanupPolicy::Conservative,
                )
                .expect_err("control character rejection"),
            );
        },
    ));
    rows.push(run_case("intent/happy_read_only", operations, || {
        black_box(classify_intent(balance.cleaned(), balance.language()).expect("classification"));
    }));
    rows.push(run_case(
        "intent/happy_complete_transfer",
        operations,
        || {
            black_box(
                classify_intent(transfer.cleaned(), transfer.language()).expect("classification"),
            );
        },
    ));
    rows.push(run_case(
        "intent/safety_negated_transfer",
        operations,
        || {
            black_box(
                classify_intent(negated.cleaned(), negated.language()).expect("classification"),
            );
        },
    ));
    rows.push(run_case(
        "intent/worst_bounded_transcript",
        operations,
        || {
            black_box(
                classify_intent(bounded.cleaned(), bounded.language()).expect("classification"),
            );
        },
    ));
    rows.push(run_case(
        "intent/failure_oversized_input",
        operations,
        || {
            black_box(
                classify_intent(&oversized_intent, TranscriptLanguage::English)
                    .expect_err("oversized input rejection"),
            );
        },
    ));
    rows.push(run_case(
        "prompt/transfer_confirmation_draft",
        operations,
        || {
            black_box(build_prompt_draft(&transfer, &transfer_intent).expect("confirmation draft"));
        },
    ));
    rows.push(run_case(
        "prompt/negated_transfer_clarification",
        operations,
        || {
            black_box(build_prompt_draft(&negated, &negated_intent).expect("clarification draft"));
        },
    ));
    rows.push(run_case("pipeline/happy_read_only", operations, || {
        let transcript = prepare_transcript("check balance");
        let intent =
            classify_intent(transcript.cleaned(), transcript.language()).expect("classification");
        black_box(build_prompt_draft(&transcript, &intent).expect("draft"));
    }));
    rows.push(run_case(
        "pipeline/happy_transfer_review",
        operations,
        || {
            let transcript = prepare_transcript("transfer 250 to Rahul");
            let intent = classify_intent(transcript.cleaned(), transcript.language())
                .expect("classification");
            black_box(build_prompt_draft(&transcript, &intent).expect("draft"));
        },
    ));
    rows.push(run_case(
        "pipeline/safety_negated_transfer",
        operations,
        || {
            let transcript = prepare_transcript("do not transfer 250 to Rahul");
            let intent = classify_intent(transcript.cleaned(), transcript.language())
                .expect("classification");
            black_box(build_prompt_draft(&transcript, &intent).expect("clarification"));
        },
    ));
    rows.push(run_case(
        "pipeline/worst_bounded_text_to_classification",
        operations,
        || {
            let transcript = prepare_transcript(&bounded_raw);
            black_box(
                classify_intent(transcript.cleaned(), transcript.language())
                    .expect("classification"),
            );
        },
    ));

    if wants_case("dsp/native_frame_and_drain_events") {
        let mut engine = VoiceEngine::new(EngineConfig::default())?;
        engine.select_language("en")?;
        engine.start()?;
        let abi = engine.native_abi();
        if abi.frame_samples as usize != BENCH_FRAME_SAMPLES {
            return Err("native ABI frame size differs from benchmark contract".into());
        }
        let memory = engine.memory_report();
        if memory.total_accounted_bytes > memory.budget_bytes || memory.budget_bytes > 100 * 1024 {
            return Err("native DSP working set exceeded the 100 KiB ceiling".into());
        }
        let mut frame = [0_i16; BENCH_FRAME_SAMPLES];
        for (index, sample) in frame.iter_mut().enumerate() {
            let phase = index as f32 * std::f32::consts::TAU * 220.0 / abi.sample_rate_hz as f32;
            *sample = (phase.sin() * 8_000.0) as i16;
        }
        let mut frame_index = 0_u64;
        rows.push(run_case(
            "dsp/native_frame_and_drain_events",
            operations,
            || {
                let result = engine
                    .process_pcm16_frame(
                        black_box(&frame),
                        frame_index.saturating_mul(10_000_000),
                        frame_index.saturating_mul(abi.hop_samples as u64),
                    )
                    .expect("valid native frame");
                black_box(result);
                while let Some(event) = engine.try_pop_event().expect("event queue read") {
                    black_box(event);
                }
                frame_index = frame_index.wrapping_add(1);
            },
        ));
        println!("MEMORY native_accounted_bytes={} native_budget_bytes={} native_alignment_slack_bytes={}", memory.total_accounted_bytes, memory.budget_bytes, memory.alignment_slack_bytes);
        engine.stop()?;
    }

    if wants_case("dsp/native_frame_saturated_queue_failure") {
        let mut engine = VoiceEngine::new(EngineConfig {
            event_capacity: 2,
            ..EngineConfig::default()
        })?;
        engine.select_language("en")?;
        engine.start()?;
        let abi = engine.native_abi();
        if abi.frame_samples as usize != BENCH_FRAME_SAMPLES {
            return Err("native ABI frame size differs from benchmark contract".into());
        }
        let mut frame = [0_i16; BENCH_FRAME_SAMPLES];
        for (index, sample) in frame.iter_mut().enumerate() {
            let phase = index as f32 * std::f32::consts::TAU * 220.0 / abi.sample_rate_hz as f32;
            *sample = (phase.sin() * 8_000.0) as i16;
        }
        let mut saturation_seen = false;
        for index in 0..2048_u64 {
            if engine
                .process_pcm16_frame(
                    &frame,
                    index.saturating_mul(10_000_000),
                    index.saturating_mul(abi.hop_samples as u64),
                )
                .is_err()
            {
                saturation_seen = true;
                break;
            }
        }
        if !saturation_seen {
            return Err("native queue saturation was not reached; verify queue semantics".into());
        }
        let memory = engine.memory_report();
        if memory.total_accounted_bytes > memory.budget_bytes || memory.budget_bytes > 100 * 1024 {
            return Err("saturated native DSP working set exceeded the 100 KiB ceiling".into());
        }
        let mut frame_index = 2048_u64;
        rows.push(run_case(
            "dsp/native_frame_saturated_queue_failure",
            operations,
            || {
                let result = engine.process_pcm16_frame(
                    black_box(&frame),
                    frame_index.saturating_mul(10_000_000),
                    frame_index.saturating_mul(abi.hop_samples as u64),
                );
                black_box(result.is_err());
                frame_index = frame_index.wrapping_add(1);
            },
        ));
        println!("MEMORY saturated_native_accounted_bytes={} saturated_native_budget_bytes={} saturated_alignment_slack_bytes={}", memory.total_accounted_bytes, memory.budget_bytes, memory.alignment_slack_bytes);
        engine.shutdown();
    }

    println!(
        "MEMORY rust_global_allocator_current_live_bytes={} process_vm_rss={} process_vm_hwm={}",
        LIVE_BYTES.load(Ordering::Relaxed),
        linux_status_value("VmRSS").unwrap_or_else(|| "unavailable".to_owned()),
        linux_status_value("VmHWM").unwrap_or_else(|| "unavailable".to_owned())
    );

    if let Ok(path) = env::var("FINTALLY_ALLOC_CSV") {
        write_csv(&path, &rows)?;
        println!("OUTPUT csv={}", path);
    }
    Ok(())
}
