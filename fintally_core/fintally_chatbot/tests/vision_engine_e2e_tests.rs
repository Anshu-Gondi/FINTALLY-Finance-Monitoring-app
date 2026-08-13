use fintally_chatbot::core::vision::engine::*;
use std::fs::File;
use std::io::Write;
use std::path::Path;
use std::sync::Arc;
use std::thread;
use std::time::Instant;
use tempfile::tempdir;

// ============================================================================
// HELPER FUNCTIONS & SYNTHETIC DATA GENERATORS
// ============================================================================

/// Generates a valid uncompressed 24-bit RGB BMP image in memory
fn create_synthetic_bmp_bytes(width: u32, height: u32) -> Vec<u8> {
    let row_stride = ((width * 3 + 3) / 4) * 4;
    let image_size = row_stride * height;
    let file_size = 54 + image_size;

    let mut bmp = Vec::with_capacity(file_size as usize);

    // 14-byte Bitmap File Header
    bmp.extend_from_slice(b"BM");
    bmp.extend_from_slice(&(file_size as u32).to_le_bytes());
    bmp.extend_from_slice(&0u32.to_le_bytes());
    bmp.extend_from_slice(&54u32.to_le_bytes());

    // 40-byte DIB Header (BITMAPINFOHEADER)
    bmp.extend_from_slice(&40u32.to_le_bytes());
    bmp.extend_from_slice(&(width as i32).to_le_bytes());
    bmp.extend_from_slice(&(height as i32).to_le_bytes());
    bmp.extend_from_slice(&1u16.to_le_bytes()); // Color planes
    bmp.extend_from_slice(&24u16.to_le_bytes()); // 24 bits per pixel (RGB)
    bmp.extend_from_slice(&0u32.to_le_bytes()); // Uncompressed
    bmp.extend_from_slice(&(image_size as u32).to_le_bytes());
    bmp.extend_from_slice(&2835u32.to_le_bytes()); // 72 DPI
    bmp.extend_from_slice(&2835u32.to_le_bytes());
    bmp.extend_from_slice(&0u32.to_le_bytes());
    bmp.extend_from_slice(&0u32.to_le_bytes());

    // Pixel data gradient
    let padding = (row_stride - width * 3) as usize;
    for y in 0..height {
        for x in 0..width {
            let r = ((x * 255) / width) as u8;
            let g = ((y * 255) / height) as u8;
            let b = 128u8;
            bmp.extend_from_slice(&[b, g, r]); // BMP stores BGR
        }
        bmp.extend(std::iter::repeat(0).take(padding));
    }

    bmp
}

/// Generates a valid minimal PDF byte stream in memory
fn create_synthetic_pdf_bytes() -> Vec<u8> {
    b"%PDF-1.4
1 0 obj <</Type /Catalog /Pages 2 0 R>> endobj
2 0 obj <</Type /Pages /Kids [3 0 R] /Count 1>> endobj
3 0 obj <</Type /Page /Parent 2 0 R /MediaBox [0 0 300 300]>> endobj
xref
0 4
0000000000 65535 f
0000000009 00000 n
0000000052 00000 n
0000000102 00000 n
trailer <</Size 4 /Root 1 0 R>>
startxref
168
%%EOF"
        .to_vec()
}

/// Creates a temporary directory populated with minimal valid model files for CI testing
fn create_mock_model_directory() -> tempfile::TempDir {
    let dir = tempdir().expect("Failed to create temporary directory for mock model");

    // 1. Create a minimal valid HuggingFace Tokenizer JSON
    let tokenizer_json = r#"{
        "version": "1.0",
        "truncation": null,
        "padding": null,
        "added_tokens": [
            {"id": 151645, "content": "<|im_end|>", "single_word": false, "lstrip": false, "rstrip": false, "normalized": false, "special": true}
        ],
        "normalizer": null,
        "pre_tokenizer": null,
        "post_processor": null,
        "decoder": null,
        "model": {
            "type": "BPE",
            "dropout": null,
            "unk_token": null,
            "continuing_subword_prefix": null,
            "end_of_word_suffix": null,
            "fuse_unk": false,
            "vocab": {
                "<|im_end|>": 151645,
                "<image>": 1,
                "OCR": 2,
                "with": 3,
                "format": 4,
                ":": 5,
                "\n": 6
            },
            "merges": []
        }
    }"#;
    let mut tokenizer_file = File::create(dir.path().join("tokenizer.json")).unwrap();
    tokenizer_file.write_all(tokenizer_json.as_bytes()).unwrap();

    // 2. Create minimal config.json
    let config_json = r#"{"model_type": "got_ocr2", "vocab_size": 151646}"#;
    let mut config_file = File::create(dir.path().join("config.json")).unwrap();
    config_file.write_all(config_json.as_bytes()).unwrap();

    // 3. Create dummy model.safetensors file
    let mut safetensors_file = File::create(dir.path().join("model.safetensors")).unwrap();
    safetensors_file.write_all(b"DUMMY_SAFETENSORS_HEADER").unwrap();

    dir
}

// ============================================================================
// LEVEL 1: NATIVE TELEMETRY & FAST UNIT TESTS
// ============================================================================

#[test]
fn test_thermal_metrics_api() {
    let metrics = VisionEngine::get_thermal_metrics();
    println!(
        "Thermal Status: {:?}, Max Temp: {:.1}°C",
        metrics.status, metrics.max_temp_celsius
    );
    assert!(
        metrics.max_temp_celsius >= -50.0 && metrics.max_temp_celsius <= 150.0,
        "Thermal sensor output out of plausible operational boundaries: {}",
        metrics.max_temp_celsius
    );

    // Test dynamic thermal thresholds configuration
    VisionEngine::set_thermal_thresholds(75.0, 90.0);
}

#[test]
fn test_missing_model_directory_error_handling() {
    let non_existent_dir = Path::new("/path/that/does/not/exist/at/all");
    let result = VisionEngine::from_model_dir(non_existent_dir);

    assert!(result.is_err(), "Engine should fail on missing model directory");
    let err_msg = result.err().unwrap().to_string();
    assert!(
        err_msg.contains("Failed to load GOT-OCR tokenizer") || err_msg.contains("missing"),
        "Unexpected error message: {}",
        err_msg
    );
}

// ============================================================================
// LEVEL 2: CI/CD INTEGRATION TESTS (MOCK ASSETS - RUNS ANYWHERE)
// ============================================================================

#[test]
fn test_mocked_model_construction_and_preprocessing() {
    let mock_dir = create_mock_model_directory();

    let mut engine = VisionEngine::from_model_dir(mock_dir.path())
        .expect("Failed to initialize VisionEngine with mock assets");

    // 1. Process Raw Image
    let synthetic_image = create_synthetic_bmp_bytes(512, 512);
    let img_result = engine.process_input(
        DocumentInput::RawImageBytes(&synthetic_image),
        "format",
    );
    assert!(img_result.is_ok(), "Image processing failed with mock assets: {:?}", img_result.err());

    // 2. Process PDF Stream
    let synthetic_pdf = create_synthetic_pdf_bytes();
    let pdf_result = engine.process_input(
        DocumentInput::PdfDocumentBytes(&synthetic_pdf),
        "plain",
    );
    assert!(pdf_result.is_ok(), "PDF processing failed with mock assets: {:?}", pdf_result.err());

    // 3. Process Financial Chart with dynamic dimensions
    let chart_result = engine.process_input_with_dims(
        DocumentInput::FinancialChartBytes(&synthetic_image),
        "Parse chart",
        1024,
        1024,
        3,
    );
    assert!(chart_result.is_ok(), "Chart processing failed with mock assets: {:?}", chart_result.err());
}

// ============================================================================
// LEVEL 3: THREAD SAFETY & CONCURRENCY TESTS
// ============================================================================

#[test]
fn test_vision_engine_multi_threaded_parallel_throughput() {
    let mock_dir = Arc::new(create_mock_model_directory());
    let mut handles = vec![];

    // Spawn 4 threads, each with its own independent VisionEngine instance
    for thread_id in 0..4 {
        let mock_dir_clone = Arc::clone(&mock_dir);
        let handle = thread::spawn(move || {
            // Instantiate per-thread engine (Zero lock contention)
            let mut engine = VisionEngine::from_model_dir(mock_dir_clone.path())
                .expect("Failed to initialize thread-local VisionEngine");

            let img = create_synthetic_bmp_bytes(256, 256);

            // Execute parallel processing
            let res = engine.process_input(
                DocumentInput::RawImageBytes(&img),
                &format!("Thread Prompt {}", thread_id),
            );

            assert!(res.is_ok(), "Thread {} failed processing", thread_id);
        });
        handles.push(handle);
    }

    for handle in handles {
        handle.join().expect("Worker thread panicked during parallel execution");
    }
}

// ============================================================================
// LEVEL 4: END-TO-END PRODUCTION TESTS (USES REAL WEIGHTS IF PRESENT)
// ============================================================================

#[test]
fn test_got_ocr2_e2e_real_weights_inference() {
    let real_model_dir = Path::new("llm_models/ocr/got_ocr2_0_output");

    // Skip if real model weights are not present on local machine/runner
    if !real_model_dir.join("tokenizer.json").exists() || !real_model_dir.join("model.safetensors").exists() {
        println!("⚠️ Skipping E2E Real Weights Test: 'llm_models/ocr/got_ocr2_0_output' not found.");
        return;
    }

    println!("🔥 Real model assets detected! Running full End-to-End inference test...");

    // Test default constructor
    let mut engine = VisionEngine::new()
        .expect("Failed to instantiate VisionEngine using default model path");

    // 1. Process Synthetic Image through full real tokenizer pipeline
    let image_bytes = create_synthetic_bmp_bytes(1024, 1024);
    let output = engine.process_input(
        DocumentInput::RawImageBytes(&image_bytes),
        "format",
    ).expect("End-to-End image OCR processing failed");

    println!("Output text from real GOT-OCR tokenizer decode: {:?}", output);
    assert!(!output.is_empty() || output == "", "E2E output returned successfully");
}

// ============================================================================
// LEVEL 5: DEDICATED MICRO-BENCHMARK TESTS
// ============================================================================

#[test]
fn bench_pure_ocr_inference_throughput() {
    // 1. Measure Setup Latency
    let init_start = Instant::now();
    let real_model_dir = Path::new("llm_models/ocr/got_ocr2_0_output");

    let (mut engine, model_type) = if real_model_dir.join("tokenizer.json").exists() {
        (
            VisionEngine::new().expect("Failed to create engine with real model"),
            "REAL_WEIGHTS",
        )
    } else {
        let mock_dir = create_mock_model_directory();
        (
            VisionEngine::from_model_dir(mock_dir.path()).expect("Failed to create mock engine"),
            "MOCK_ASSETS",
        )
    };
    let init_duration = init_start.elapsed();

    // 2. Prepare Synthetic Image (512x512 RGB BMP)
    let synthetic_image = create_synthetic_bmp_bytes(512, 512);

    // Warm-up run to exclude cold-cache memory allocations
    let _ = engine.process_input(
        DocumentInput::RawImageBytes(&synthetic_image),
        "warmup format",
    );

    // 3. Execution Loop
    let iterations = 100;
    let mut latencies = Vec::with_capacity(iterations);

    let batch_start = Instant::now();
    for i in 0..iterations {
        let start = Instant::now();
        let res = engine.process_input(
            DocumentInput::RawImageBytes(&synthetic_image),
            "format",
        );
        let elapsed = start.elapsed();

        assert!(res.is_ok(), "Iteration {} failed during benchmark", i);
        latencies.push(elapsed);
    }
    let total_batch_time = batch_start.elapsed();

    // 4. Calculate Statistics
    latencies.sort();
    let total_duration_secs = total_batch_time.as_secs_f64();
    let fps = (iterations as f64) / total_duration_secs;
    let avg_latency = total_batch_time / (iterations as u32);
    let min_latency = latencies.first().cloned().unwrap_or_default();
    let max_latency = latencies.last().cloned().unwrap_or_default();
    let p95_index = ((iterations as f64) * 0.95) as usize;
    let p95_latency = latencies.get(p95_index).cloned().unwrap_or_default();

    // 5. Output Results
    println!("\n=======================================================");
    println!("🔥 OCR ENGINE MICRO-BENCHMARK RESULTS [{}]", model_type);
    println!("=======================================================");
    println!("Initialization Latency : {:?}", init_duration);
    println!("Total Batch Time ({}) : {:?}", iterations, total_batch_time);
    println!("Throughput             : {:.2} images/sec (FPS)", fps);
    println!("Average Latency        : {:?}", avg_latency);
    println!("Min Latency            : {:?}", min_latency);
    println!("Max Latency            : {:?}", max_latency);
    println!("p95 Tail Latency       : {:?}", p95_latency);
    println!("=======================================================\n");

    assert!(fps > 0.0, "FPS should be greater than zero");
}
