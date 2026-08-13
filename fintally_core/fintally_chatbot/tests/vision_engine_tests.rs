use fintally_chatbot::core::vision::engine::*;
use std::path::Path;

/// Generates a valid uncompressed 24-bit RGB BMP image in memory (no external image libraries needed)
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

    // Fill pixel data with a synthetic gradient/chart grid pattern
    let padding = (row_stride - width * 3) as usize;
    for y in 0..height {
        for x in 0..width {
            let r = ((x * 255) / width) as u8;
            let g = ((y * 255) / height) as u8;
            let b = 128u8;
            bmp.extend_from_slice(&[b, g, r]); // BMP stores BGR format
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

#[test]
fn test_thermal_metrics_api() {
    // 1. Check thermal metric reading
    let metrics = VisionEngine::get_thermal_metrics();
    println!(
        "Thermal Status: {:?}, Max Temp: {:.1}°C",
        metrics.status, metrics.max_temp_celsius
    );
    assert!(
        metrics.max_temp_celsius >= -50.0,
        "Invalid thermal sensor output"
    );

    // 2. Test thermal thresholds configuration
    VisionEngine::set_thermal_thresholds(75.0, 90.0);
}

#[test]
fn test_got_ocr2_engine_construction() {
    let model_dir = Path::new("llm_models/ocr/got_ocr2_0_output");

    if !model_dir.join("tokenizer.json").exists() {
        println!("Skipping engine constructor test: GOT-OCR 2.0 assets not downloaded");
        return;
    }

    // Test default constructor pointing to GOT-OCR 2.0 directory
    let default_engine = VisionEngine::new();
    assert!(
        default_engine.is_ok(),
        "Failed to instantiate VisionEngine::new(): {:?}",
        default_engine.err()
    );
}

#[test]
fn test_got_ocr2_synthetic_inputs() {
    let model_dir = Path::new("llm_models/ocr/got_ocr2_0_output");

    // Skip inference tests if HuggingFace GOT-OCR weights aren't present
    if !model_dir.join("tokenizer.json").exists() {
        println!("Skipping GOT-OCR 2.0 inference test: model_dir assets not present");
        return;
    }

    let mut engine = VisionEngine::from_model_dir(model_dir)
        .expect("Failed to construct VisionEngine from GOT-OCR 2.0 model directory");

    // 1. Test Raw Synthetic Image Input with GOT-OCR formatted prompt ("format")
    let synthetic_image = create_synthetic_bmp_bytes(1024, 1024);
    let img_result = engine.process_input(
        DocumentInput::RawImageBytes(&synthetic_image),
        "format",
    );
    assert!(
        img_result.is_ok(),
        "GOT-OCR 2.0 image processing failed: {:?}",
        img_result.err()
    );

    // 2. Test Synthetic PDF Stream Input with GOT-OCR plain text prompt ("plain")
    let synthetic_pdf = create_synthetic_pdf_bytes();
    let pdf_result = engine.process_input(
        DocumentInput::PdfDocumentBytes(&synthetic_pdf),
        "plain",
    );
    assert!(
        pdf_result.is_ok(),
        "GOT-OCR 2.0 PDF processing failed: {:?}",
        pdf_result.err()
    );

    // 3. Test Synthetic Financial Chart Input with GOT-OCR custom target dimensions (1024x1024)
    let synthetic_chart = create_synthetic_bmp_bytes(1024, 1024);
    let chart_result = engine.process_input_with_dims(
        DocumentInput::FinancialChartBytes(&synthetic_chart),
        "Parse financial table into Markdown",
        1024,
        1024,
        3,
    );
    assert!(
        chart_result.is_ok(),
        "GOT-OCR 2.0 Chart processing failed: {:?}",
        chart_result.err()
    );
}
