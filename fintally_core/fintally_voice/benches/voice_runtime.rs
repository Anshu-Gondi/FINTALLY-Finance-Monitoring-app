//! Reproducible Rust-side benchmarks for FinTally Voice.
//!
//! This is a standalone benchmark binary (`harness = false`) with no extra
//! crate dependencies. Percentiles describe per-batch average ns/op, not an
//! instrumented single-call latency distribution. Use the allocation benchmark
//! for heap-request counts, and the perf script for CPU/cache hardware counters.

use std::env;
use std::fs;
use std::hint::black_box;
use std::io::{self, Write};
use std::path::Path;
use std::time::{Duration, Instant};

use fintally_voice::engine::{EngineConfig, VoiceEngine};
use fintally_voice::intent::{classify_intent, MAX_INTENT_TEXT_BYTES};
use fintally_voice::prompt::build_prompt_draft;
use fintally_voice::transcript::{
    clean_transcript, CleanupPolicy, Transcript, TranscriptLanguage, MAX_RAW_TRANSCRIPT_BYTES,
};

const BENCH_FRAME_SAMPLES: usize = 320;

struct BenchResult {
    name: String,
    ops: u64,
    mean_ns: f64,
    p50_ns: f64,
    p95_ns: f64,
    p99_ns: f64,
    min_ns: f64,
    max_ns: f64,
}

fn percentile(sorted: &[f64], q: f64) -> f64 {
    if sorted.is_empty() {
        return 0.0;
    }
    let index = (((sorted.len() - 1) as f64) * q).ceil() as usize;
    sorted[index.min(sorted.len() - 1)]
}

fn wants_case(name: &str) -> bool {
    match env::var("FINTALLY_BENCH_FILTER") {
        Ok(filter) if !filter.is_empty() => name.contains(&filter),
        _ => true,
    }
}

fn run_case<F>(name: &str, sample_count: usize, batch_size: usize, mut operation: F) -> BenchResult
where
    F: FnMut(),
{
    if !wants_case(name) {
        println!(
            "SKIP name={} filter={}",
            name,
            env::var("FINTALLY_BENCH_FILTER").unwrap_or_default()
        );
        return BenchResult {
            name: name.to_owned(),
            ops: 0,
            mean_ns: 0.0,
            p50_ns: 0.0,
            p95_ns: 0.0,
            p99_ns: 0.0,
            min_ns: 0.0,
            max_ns: 0.0,
        };
    }

    let warmup_ops = batch_size.saturating_mul(3).min(10_000);
    for _ in 0..warmup_ops {
        operation();
    }

    // Reserve the sample vector before starting the timed loop.
    let mut samples_ns_per_op = Vec::with_capacity(sample_count);
    let mut total_elapsed = Duration::ZERO;
    for _ in 0..sample_count {
        let start = Instant::now();
        for _ in 0..batch_size {
            operation();
        }
        let elapsed = start.elapsed();
        total_elapsed += elapsed;
        samples_ns_per_op.push(elapsed.as_nanos() as f64 / batch_size as f64);
    }
    samples_ns_per_op.sort_by(f64::total_cmp);

    let ops = (sample_count as u64).saturating_mul(batch_size as u64);
    let mean_ns = total_elapsed.as_nanos() as f64 / ops.max(1) as f64;
    let result = BenchResult {
        name: name.to_owned(),
        ops,
        mean_ns,
        p50_ns: percentile(&samples_ns_per_op, 0.50),
        p95_ns: percentile(&samples_ns_per_op, 0.95),
        p99_ns: percentile(&samples_ns_per_op, 0.99),
        min_ns: samples_ns_per_op.first().copied().unwrap_or(0.0),
        max_ns: samples_ns_per_op.last().copied().unwrap_or(0.0),
    };
    print_result(&result);
    result
}

fn print_result(result: &BenchResult) {
    println!(
        "BENCH name={} ops={} mean_ns_op={:.2} p50_batch_ns_op={:.2} p95_batch_ns_op={:.2} p99_batch_ns_op={:.2} min_batch_ns_op={:.2} max_batch_ns_op={:.2} ops_s={:.2}",
        result.name,
        result.ops,
        result.mean_ns,
        result.p50_ns,
        result.p95_ns,
        result.p99_ns,
        result.min_ns,
        result.max_ns,
        if result.mean_ns > 0.0 { 1_000_000_000.0 / result.mean_ns } else { 0.0 },
    );
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

fn write_csv(path: &str, results: &[BenchResult]) -> io::Result<()> {
    let output_path = Path::new(path);
    if let Some(parent) = output_path.parent() {
        if !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent)?;
        }
    }
    let mut file = fs::File::create(output_path)?;
    writeln!(
        file,
        "name,ops,mean_ns_op,p50_batch_ns_op,p95_batch_ns_op,p99_batch_ns_op,min_batch_ns_op,max_batch_ns_op,ops_per_second"
    )?;
    for r in results.iter().filter(|r| r.ops > 0) {
        writeln!(
            file,
            "{},{},{:.3},{:.3},{:.3},{:.3},{:.3},{:.3},{:.3}",
            r.name,
            r.ops,
            r.mean_ns,
            r.p50_ns,
            r.p95_ns,
            r.p99_ns,
            r.min_ns,
            r.max_ns,
            if r.mean_ns > 0.0 {
                1_000_000_000.0 / r.mean_ns
            } else {
                0.0
            },
        )?;
    }
    Ok(())
}

fn linux_status_value(key: &str) -> Option<String> {
    let status = fs::read_to_string("/proc/self/status").ok()?;
    status.lines().find_map(|line| {
        let (found, value) = line.split_once(':')?;
        (found == key).then(|| value.trim().to_owned())
    })
}

fn cpu_model() -> Option<String> {
    let cpuinfo = fs::read_to_string("/proc/cpuinfo").ok()?;
    cpuinfo.lines().find_map(|line| {
        let (key, value) = line.split_once(':')?;
        (key.trim() == "model name").then(|| value.trim().to_owned())
    })
}

fn optional_sysfs_value(path: &str) -> Option<String> {
    fs::read_to_string(path).ok().map(|s| s.trim().to_owned())
}

fn print_system_info() {
    println!(
        "SYSTEM os={} arch={} logical_cpus={}",
        env::consts::OS,
        env::consts::ARCH,
        std::thread::available_parallelism()
            .map(|n| n.get())
            .unwrap_or(0)
    );
    println!(
        "SYSTEM cpu_model={}",
        cpu_model().unwrap_or_else(|| "unavailable".to_owned())
    );
    println!(
        "SYSTEM cpu0_scaling_cur_freq_khz={}",
        optional_sysfs_value("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq")
            .unwrap_or_else(|| "unavailable".to_owned())
    );
    println!(
        "SYSTEM cpu0_scaling_governor={}",
        optional_sysfs_value("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
            .unwrap_or_else(|| "unavailable".to_owned())
    );
    println!(
        "SYSTEM process_vm_rss={}",
        linux_status_value("VmRSS").unwrap_or_else(|| "unavailable".to_owned())
    );
    println!(
        "SYSTEM process_vm_hwm={}",
        linux_status_value("VmHWM").unwrap_or_else(|| "unavailable".to_owned())
    );
    println!(
        "SYSTEM sample_count={} batch_size={} case_filter={}",
        parse_env_usize("FINTALLY_BENCH_SAMPLES", 200, 10, 10_000),
        parse_env_usize("FINTALLY_BENCH_BATCH", 64, 1, 1_000_000),
        env::var("FINTALLY_BENCH_FILTER").unwrap_or_else(|_| "<all>".to_owned())
    );
}

fn parse_env_usize(key: &str, default: usize, min: usize, max: usize) -> usize {
    env::var(key)
        .ok()
        .and_then(|value| value.parse::<usize>().ok())
        .map(|value| value.clamp(min, max))
        .unwrap_or(default)
}

fn prepare_transcript(text: &str) -> Transcript {
    clean_transcript(
        text,
        TranscriptLanguage::English,
        CleanupPolicy::Conservative,
    )
    .expect("benchmark fixture must be valid")
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let sample_count = parse_env_usize("FINTALLY_BENCH_SAMPLES", 200, 10, 10_000);
    let batch_size = parse_env_usize("FINTALLY_BENCH_BATCH", 64, 1, 1_000_000);
    let mut results = Vec::with_capacity(16);

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
        return Err("negation benchmark fixture was not classified as negated".into());
    }
    let transfer_draft_fixture = build_prompt_draft(&transfer, &transfer_intent)?;
    if transfer_draft_fixture.deterministic_fallback().is_empty() {
        return Err("transfer fallback fixture must not be empty".into());
    }

    println!("FinTally Voice Rust runtime benchmark");
    println!("CONFIG accounted_dsp_budget_max_bytes=102400");
    print_system_info();

    results.push(run_case(
        "cleanup/happy_balance",
        sample_count,
        batch_size,
        || {
            black_box(prepare_transcript("check balance"));
        },
    ));
    results.push(run_case(
        "cleanup/worst_bounded_transcript",
        sample_count,
        batch_size,
        || {
            black_box(
                clean_transcript(
                    &bounded_raw,
                    TranscriptLanguage::English,
                    CleanupPolicy::Conservative,
                )
                .expect("bounded fixture remains valid"),
            );
        },
    ));
    results.push(run_case(
        "cleanup/failure_control_character",
        sample_count,
        batch_size,
        || {
            black_box(
                clean_transcript(
                    invalid_control,
                    TranscriptLanguage::English,
                    CleanupPolicy::Conservative,
                )
                .expect_err("control character must be rejected"),
            );
        },
    ));

    results.push(run_case(
        "intent/happy_read_only",
        sample_count,
        batch_size,
        || {
            black_box(
                classify_intent(balance.cleaned(), balance.language())
                    .expect("valid intent fixture"),
            );
        },
    ));
    results.push(run_case(
        "intent/happy_complete_transfer",
        sample_count,
        batch_size,
        || {
            black_box(
                classify_intent(transfer.cleaned(), transfer.language())
                    .expect("valid transfer fixture"),
            );
        },
    ));
    results.push(run_case(
        "intent/safety_negated_transfer",
        sample_count,
        batch_size,
        || {
            black_box(
                classify_intent(negated.cleaned(), negated.language())
                    .expect("valid negated fixture"),
            );
        },
    ));
    results.push(run_case(
        "intent/worst_bounded_transcript",
        sample_count,
        batch_size,
        || {
            black_box(
                classify_intent(bounded.cleaned(), bounded.language())
                    .expect("bounded text is accepted"),
            );
        },
    ));
    results.push(run_case(
        "intent/failure_oversized_input",
        sample_count,
        batch_size,
        || {
            black_box(
                classify_intent(&oversized_intent, TranscriptLanguage::English)
                    .expect_err("oversized intent input must be rejected"),
            );
        },
    ));

    results.push(run_case(
        "prompt/transfer_confirmation_draft",
        sample_count,
        batch_size,
        || {
            black_box(
                build_prompt_draft(&transfer, &transfer_intent).expect("valid transfer draft"),
            );
        },
    ));
    results.push(run_case(
        "prompt/negated_transfer_clarification",
        sample_count,
        batch_size,
        || {
            black_box(
                build_prompt_draft(&negated, &negated_intent).expect("valid clarification draft"),
            );
        },
    ));

    results.push(run_case(
        "pipeline/happy_read_only",
        sample_count,
        batch_size,
        || {
            let transcript = prepare_transcript("check balance");
            let intent = classify_intent(transcript.cleaned(), transcript.language())
                .expect("classification");
            black_box(build_prompt_draft(&transcript, &intent).expect("prompt draft"));
        },
    ));
    results.push(run_case(
        "pipeline/happy_transfer_review",
        sample_count,
        batch_size,
        || {
            let transcript = prepare_transcript("transfer 250 to Rahul");
            let intent = classify_intent(transcript.cleaned(), transcript.language())
                .expect("classification");
            let draft = build_prompt_draft(&transcript, &intent).expect("confirmation draft");
            black_box(draft);
        },
    ));
    results.push(run_case(
        "pipeline/safety_negated_transfer",
        sample_count,
        batch_size,
        || {
            let transcript = prepare_transcript("do not transfer 250 to Rahul");
            let intent = classify_intent(transcript.cleaned(), transcript.language())
                .expect("classification");
            black_box(intent.negated);
            black_box(build_prompt_draft(&transcript, &intent).expect("clarification draft"));
        },
    ));
    results.push(run_case(
        "pipeline/worst_bounded_text_to_classification",
        sample_count,
        batch_size,
        || {
            let transcript = prepare_transcript(&bounded_raw);
            black_box(
                classify_intent(transcript.cleaned(), transcript.language())
                    .expect("classification"),
            );
        },
    ));

    // Native DSP cases can be selected individually for hardware-counter runs.
    if wants_case("dsp/native_frame_and_drain_events") {
        let mut native = VoiceEngine::new(EngineConfig::default())?;
        native.select_language("en")?;
        native.start()?;
        let abi = native.native_abi();
        let memory = native.memory_report();
        if memory.total_accounted_bytes > memory.budget_bytes || memory.budget_bytes > 100 * 1024 {
            return Err("native DSP memory accounting exceeded the 100 KiB ceiling".into());
        }
        if abi.frame_samples as usize != BENCH_FRAME_SAMPLES {
            return Err("native ABI frame size differs from benchmark contract".into());
        }
        let mut frame = [0_i16; BENCH_FRAME_SAMPLES];
        for (index, sample) in frame.iter_mut().enumerate() {
            let phase = (index as f32) * std::f32::consts::TAU * 220.0 / abi.sample_rate_hz as f32;
            *sample = (phase.sin() * 8_000.0) as i16;
        }
        let mut frame_index = 0_u64;
        results.push(run_case(
            "dsp/native_frame_and_drain_events",
            sample_count,
            batch_size,
            || {
                let sample_position = frame_index.saturating_mul(abi.hop_samples as u64);
                let timestamp_ns = frame_index.saturating_mul(10_000_000);
                let summary = native
                    .process_pcm16_frame(black_box(&frame), timestamp_ns, sample_position)
                    .expect("native frame processing should succeed when events are drained");
                black_box(summary);
                while let Some(event) = native.try_pop_event().expect("event queue read") {
                    black_box(event);
                }
                frame_index = frame_index.wrapping_add(1);
            },
        ));
        println!("MEMORY native_accounted_bytes={} native_budget_bytes={} native_dsp_workspace_bytes={} native_pipeline_workspace_bytes={} native_event_storage_bytes={} native_text_storage_bytes={} native_context_bytes={} native_codebook_bytes={} native_frame_transient_bytes={} native_alignment_slack_bytes={}",
            memory.total_accounted_bytes, memory.budget_bytes, memory.dsp_workspace_bytes,
            memory.pipeline_workspace_bytes, memory.event_storage_bytes, memory.text_storage_bytes,
            memory.native_context_bytes, memory.active_codebook_bytes, memory.one_frame_transient_bytes,
            memory.alignment_slack_bytes);
        native.stop()?;
    }

    // Deliberately saturate a small native event queue to exercise the failure
    // path. This is a stress measurement, not a normal operating mode.
    if wants_case("dsp/native_frame_saturated_queue_failure") {
        let mut saturated = VoiceEngine::new(EngineConfig {
            event_capacity: 2,
            ..EngineConfig::default()
        })?;
        saturated.select_language("en")?;
        saturated.start()?;
        let abi = saturated.native_abi();
        if abi.frame_samples as usize != BENCH_FRAME_SAMPLES {
            return Err("native ABI frame size differs from benchmark contract".into());
        }
        let memory = saturated.memory_report();
        if memory.total_accounted_bytes > memory.budget_bytes || memory.budget_bytes > 100 * 1024 {
            return Err(
                "saturated native DSP memory accounting exceeded the 100 KiB ceiling".into(),
            );
        }
        println!("MEMORY saturated_native_accounted_bytes={} saturated_native_budget_bytes={} saturated_event_storage_bytes={} saturated_alignment_slack_bytes={}",
            memory.total_accounted_bytes, memory.budget_bytes, memory.event_storage_bytes, memory.alignment_slack_bytes);
        let mut frame = [0_i16; BENCH_FRAME_SAMPLES];
        for (index, sample) in frame.iter_mut().enumerate() {
            let phase = (index as f32) * std::f32::consts::TAU * 220.0 / abi.sample_rate_hz as f32;
            *sample = (phase.sin() * 8_000.0) as i16;
        }
        let mut saturated_at = None;
        for index in 0..2048_u64 {
            if saturated
                .process_pcm16_frame(
                    &frame,
                    index.saturating_mul(10_000_000),
                    index.saturating_mul(160),
                )
                .is_err()
            {
                saturated_at = Some(index);
                break;
            }
        }
        if saturated_at.is_none() {
            return Err("native queue saturation was not reached; update this benchmark to match queue semantics".into());
        }
        let mut saturated_index = saturated_at.unwrap_or(0);
        results.push(run_case(
            "dsp/native_frame_saturated_queue_failure",
            sample_count,
            batch_size,
            || {
                let sample_position = saturated_index.saturating_mul(abi.hop_samples as u64);
                let timestamp_ns = saturated_index.saturating_mul(10_000_000);
                let result =
                    saturated.process_pcm16_frame(black_box(&frame), timestamp_ns, sample_position);
                black_box(result.is_err());
                saturated_index = saturated_index.wrapping_add(1);
            },
        ));
        let saturated_stats = saturated.stats();
        println!("NATIVE_SATURATED stats_frames_processed={} stats_frames_published={} stats_event_publish_failures={} stats_events_popped={} stats_native_events_dropped={} saturated_queue_first_error_frame={}",
            saturated_stats.frames_processed, saturated_stats.frames_published,
            saturated_stats.event_publish_failures, saturated_stats.events_popped,
            saturated_stats.native_events_dropped, saturated_at.unwrap_or(0));
        saturated.shutdown();
    }

    println!(
        "MEMORY process_vm_rss={} process_vm_hwm={}",
        linux_status_value("VmRSS").unwrap_or_else(|| "unavailable".to_owned()),
        linux_status_value("VmHWM").unwrap_or_else(|| "unavailable".to_owned())
    );
    println!(
        "SYSTEM cpu0_scaling_cur_freq_khz_after={}",
        optional_sysfs_value("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq")
            .unwrap_or_else(|| "unavailable".to_owned())
    );

    if let Ok(path) = env::var("FINTALLY_BENCH_CSV") {
        write_csv(&path, &results)?;
        println!("OUTPUT csv={}", path);
    }
    Ok(())
}
