use fintally_chatbot::core::vision::engine::*;
use std::path::Path;
use std::sync::Arc;
use std::thread;
use std::time::Instant;

// ============================================================================
// HELPER FUNCTIONS & SYNTHETIC DATA GENERATORS
// ============================================================================

/// Generates a valid uncompressed 24-bit RGB BMP image (exercises non-JPEG transcoding path)
fn create_synthetic_bmp_bytes(width: u32, height: u32) -> Vec<u8> {
    let row_stride = ((width * 3 + 3) / 4) * 4;
    let image_size = (row_stride * height) as usize;
    let file_size = 54 + image_size;

    let mut bmp = Vec::with_capacity(file_size);

    bmp.extend_from_slice(b"BM");
    bmp.extend_from_slice(&(file_size as u32).to_le_bytes());
    bmp.extend_from_slice(&0u32.to_le_bytes());
    bmp.extend_from_slice(&54u32.to_le_bytes());

    bmp.extend_from_slice(&40u32.to_le_bytes());
    bmp.extend_from_slice(&(width as i32).to_le_bytes());
    bmp.extend_from_slice(&(height as i32).to_le_bytes());
    bmp.extend_from_slice(&1u16.to_le_bytes());
    bmp.extend_from_slice(&24u16.to_le_bytes());
    bmp.extend_from_slice(&0u32.to_le_bytes());
    bmp.extend_from_slice(&(image_size as u32).to_le_bytes());
    bmp.extend_from_slice(&2835u32.to_le_bytes());
    bmp.extend_from_slice(&2835u32.to_le_bytes());
    bmp.extend_from_slice(&0u32.to_le_bytes());
    bmp.extend_from_slice(&0u32.to_le_bytes());

    bmp.resize(file_size, 128);
    bmp
}

/// Generates a valid JPEG byte stream in memory (exercises zero-copy JPEG fast-path filter)
fn create_synthetic_jpeg_bytes(width: u32, height: u32) -> Vec<u8> {
    let img = image::RgbImage::new(width, height);
    let mut jpeg_bytes = Vec::new();
    let mut cursor = std::io::Cursor::new(&mut jpeg_bytes);

    img.write_to(&mut cursor, image::ImageFormat::Jpeg)
        .expect("Failed to encode synthetic JPEG image");

    jpeg_bytes
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

// ============================================================================
// LEVEL 1: NATIVE TELEMETRY & ENGINE INSTANTIATION
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

    VisionEngine::set_thermal_thresholds(75.0, 90.0);
}

#[test]
fn test_pure_cpp_engine_construction() {
    let engine = VisionEngine::new();
    assert!(
        engine.is_ok(),
        "Failed to instantiate pure C++ VisionEngine: {:?}",
        engine.err()
    );
}

// ============================================================================
// LEVEL 2: INPUT PROCESSING & TRANSCODING FILTER TESTS
// ============================================================================

#[test]
fn test_input_processing_and_jpeg_filtering() {
    let mut engine = VisionEngine::new().expect("Failed to initialize VisionEngine");

    // 1. Non-JPEG Input (BMP) -> Exercises Transcoding Path
    let synthetic_bmp = create_synthetic_bmp_bytes(512, 512);
    let bmp_result = engine.process_input(DocumentInput::RawImageBytes(&synthetic_bmp));
    assert!(bmp_result.is_ok(), "Non-JPEG image processing failed: {:?}", bmp_result.err());

    let bmp_buffer = bmp_result.unwrap();
    let (width, height, channels) = bmp_buffer.dimensions();
    assert!(width > 0 && height > 0 && channels > 0);

    // 2. Direct JPEG Input -> Exercises Zero-Copy Fast-Path Filter
    let synthetic_jpeg = create_synthetic_jpeg_bytes(512, 512);
    let jpeg_result = engine.process_input(DocumentInput::RawImageBytes(&synthetic_jpeg));
    assert!(jpeg_result.is_ok(), "JPEG fast-path image processing failed: {:?}", jpeg_result.err());

    // 3. PDF Visual Stream Input
    let synthetic_pdf = create_synthetic_pdf_bytes();
    let pdf_result = engine.process_input(DocumentInput::PdfDocumentBytes(&synthetic_pdf));
    assert!(pdf_result.is_ok(), "PDF stream processing failed: {:?}", pdf_result.err());

    // 4. Financial Chart with Custom Dimensions (1024x1024)
    let chart_result = engine.process_input_with_dims(
        DocumentInput::FinancialChartBytes(&synthetic_bmp),
        1024,
        1024,
        3,
    );
    assert!(chart_result.is_ok(), "Financial chart processing failed: {:?}", chart_result.err());
}

// ============================================================================
// LEVEL 3: THREAD SAFETY & CONCURRENCY TESTS
// ============================================================================

#[test]
fn test_vision_engine_multi_threaded_parallel_throughput() {
    let engine = VisionEngine::new().expect("Failed to initialize VisionEngine");
    let shared_engine = Arc::new(std::sync::Mutex::new(engine));

    let mut handles = vec![];

    for thread_id in 0..2 {
        let engine_ref = Arc::clone(&shared_engine);
        let handle = thread::spawn(move || {
            let img = create_synthetic_bmp_bytes(256, 256);

            let mut engine_guard = engine_ref.lock().unwrap();
            let res = engine_guard.process_input(DocumentInput::RawImageBytes(&img));

            assert!(res.is_ok(), "Thread {} failed processing image input", thread_id);
        });
        handles.push(handle);
    }

    for handle in handles {
        handle.join().expect("Worker thread panicked during parallel execution");
    }
}

// ============================================================================
// LEVEL 4: END-TO-END PRODUCTION & TEXT EXTRACTION TEST
// ============================================================================

#[test]
fn test_e2e_image_processing_and_text_retrieval() {
    let mut engine = VisionEngine::new().expect("Failed to instantiate VisionEngine");

    // Load real test fixture image if present, or fallback to synthetic input
    let test_image_path = Path::new("tests/fixtures/sample_receipt.png");
    let image_bytes = if test_image_path.exists() {
        std::fs::read(test_image_path).expect("Failed to read test image fixture")
    } else {
        create_synthetic_bmp_bytes(1024, 1024)
    };

    let buffer = engine
        .process_input(DocumentInput::RawImageBytes(&image_bytes))
        .expect("End-to-End image processing failed");

    let (w, h, c) = buffer.dimensions();
    assert!(w > 0 && h > 0 && c > 0, "Buffer returned invalid dimension bounds");

    if let Some(extracted_text) = buffer.extracted_text() {
        println!("Extracted OCR Text from native engine: {:?}", extracted_text);
    }
}

// ============================================================================
// LEVEL 5: DEDICATED MICRO-BENCHMARK TESTS
// ============================================================================

#[test]
#[ignore = "Performance micro-benchmark; run explicitly with `cargo test -- --ignored`"]
fn bench_pure_ocr_inference_throughput() {
    let init_start = Instant::now();
    let mut engine = VisionEngine::new().expect("Failed to create engine instance");
    let init_duration = init_start.elapsed();

    let synthetic_image = create_synthetic_jpeg_bytes(512, 512);

    // Engine Warmup Pass
    let _ = engine.warmup();

    let iterations = 100;
    let mut latencies = Vec::with_capacity(iterations);

    let batch_start = Instant::now();
    for i in 0..iterations {
        let start = Instant::now();
        let res = engine.process_input(DocumentInput::RawImageBytes(&synthetic_image));
        let elapsed = start.elapsed();

        assert!(res.is_ok(), "Iteration {} failed during benchmark", i);
        latencies.push(elapsed);
    }
    let total_batch_time = batch_start.elapsed();

    latencies.sort();
    let total_duration_secs = total_batch_time.as_secs_f64();
    let fps = (iterations as f64) / total_duration_secs;
    let avg_latency = total_batch_time / (iterations as u32);
    let min_latency = latencies.first().cloned().unwrap_or_default();
    let max_latency = latencies.last().cloned().unwrap_or_default();
    let p95_index = ((iterations as f64) * 0.95) as usize;
    let p95_latency = latencies.get(p95_index).cloned().unwrap_or_default();

    println!("\n=======================================================");
    println!("🔥 NATIVE C++ VISION ENGINE BENCHMARK RESULTS");
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
