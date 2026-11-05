//! Manual release-mode latency report for codec + structural validation.
//!
//! Run with:
//! cargo test -p kinetib_ipc_protocol --release --test performance -- --ignored --nocapture
//!
//! This is deliberately ignored in the normal test suite and does not gate CI
//! on hard microsecond thresholds: timing depends on CPU frequency, OS load,
//! compiler, power policy, and virtualization. It reports measurements instead.

use std::hint::black_box;
use std::time::Instant;

use kinetib_ipc_protocol::{
    decode_payload, encode_payload, Command, RequestEnvelope, DEFAULT_PAGE_SIZE,
    IPC_PROTOCOL_VERSION, MAX_PAYLOAD_BYTES,
};

const WARMUP_OPERATIONS: usize = 1_000;
const OPERATIONS_PER_SAMPLE: usize = 100;
const SAMPLE_COUNT: usize = 101;

#[test]
#[ignore = "manual release-mode performance report; machine-dependent"]
fn report_codec_validation_latency_and_payload_size() {
    let request = RequestEnvelope {
        protocol_version: IPC_PROTOCOL_VERSION,
        request_id: 42,
        session_token: Some("bench-token".to_owned()),
        command: Command::ListTransactions {
            page: 1,
            page_size: DEFAULT_PAGE_SIZE,
        },
    };
    assert_eq!(request.validate(), Ok(()));

    let sample_payload = encode_payload(&request).expect("encode benchmark request");
    assert!(sample_payload.len() <= MAX_PAYLOAD_BYTES);

    for _ in 0..WARMUP_OPERATIONS {
        let bytes = encode_payload(black_box(&request)).expect("warmup encode");
        let decoded: RequestEnvelope = decode_payload(black_box(&bytes)).expect("warmup decode");
        black_box(decoded.validate()).expect("warmup validation");
    }

    let mut nanoseconds_per_operation = Vec::with_capacity(SAMPLE_COUNT);
    for _ in 0..SAMPLE_COUNT {
        let started = Instant::now();
        for _ in 0..OPERATIONS_PER_SAMPLE {
            let bytes = encode_payload(black_box(&request)).expect("encode request");
            let decoded: RequestEnvelope =
                decode_payload(black_box(&bytes)).expect("decode request");
            black_box(decoded.validate()).expect("validate request");
        }
        let elapsed = started.elapsed().as_nanos() as f64;
        nanoseconds_per_operation.push(elapsed / OPERATIONS_PER_SAMPLE as f64);
    }

    nanoseconds_per_operation.sort_by(f64::total_cmp);
    let min_us = nanoseconds_per_operation[0] / 1_000.0;
    let p50_us = nanoseconds_per_operation[SAMPLE_COUNT / 2] / 1_000.0;
    let p95_index = ((SAMPLE_COUNT - 1) as f64 * 0.95).ceil() as usize;
    let p95_us = nanoseconds_per_operation[p95_index] / 1_000.0;
    let max_us = nanoseconds_per_operation[SAMPLE_COUNT - 1] / 1_000.0;

    println!("IPC codec + validation benchmark (encode + decode + validate)");
    println!(
        "payload bytes: {} / {}",
        sample_payload.len(),
        MAX_PAYLOAD_BYTES
    );
    println!("min: {min_us:.2} µs/op | p50: {p50_us:.2} µs/op | p95: {p95_us:.2} µs/op | max: {max_us:.2} µs/op");
    println!("targets: 25–60 µs is an aggressive local-operation goal; 3–5 ms end-to-end requires a separate application/transport benchmark.");
    println!("Timing is informational and does not fail based on a machine-independent threshold.");

    assert!(min_us.is_finite() && p50_us.is_finite() && p95_us.is_finite() && max_us.is_finite());
}
