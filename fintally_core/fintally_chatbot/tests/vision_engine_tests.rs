use fintally_chatbot::core::vision::engine::*;

/// Generates a valid uncompressed 24-bit RGB BMP image in memory (exercises non-JPEG transcoding path)
fn create_synthetic_bmp_bytes(width: u32, height: u32) -> Vec<u8> {
    let img = image::RgbImage::from_fn(width, height, |x, y| {
        let r = ((x * 255) / width.max(1)) as u8;
        let g = ((y * 255) / height.max(1)) as u8;
        let b = 128u8;
        image::Rgb([r, g, b])
    });

    let mut bmp_bytes = Vec::new();
    let mut cursor = std::io::Cursor::new(&mut bmp_bytes);
    img.write_to(&mut cursor, image::ImageFormat::Bmp)
        .expect("Failed to encode synthetic BMP image");

    bmp_bytes
}

/// Generates a valid JPEG byte stream in memory (exercises zero-copy JPEG fast-path)
fn create_synthetic_jpeg_bytes(width: u32, height: u32) -> Vec<u8> {
    let img = image::RgbImage::from_fn(width, height, |x, y| {
        let r = ((x * 255) / width.max(1)) as u8;
        let g = ((y * 255) / height.max(1)) as u8;
        let b = 128u8;
        image::Rgb([r, g, b])
    });

    let mut jpeg_bytes = Vec::new();
    let mut cursor = std::io::Cursor::new(&mut jpeg_bytes);

    img.write_to(&mut cursor, image::ImageFormat::Jpeg)
        .expect("Failed to encode synthetic JPEG image");

    jpeg_bytes
}

/// Generates a valid, specification-compliant PDF 1.4 byte stream with exact object offsets
fn create_synthetic_pdf_bytes() -> Vec<u8> {
    let mut pdf = Vec::new();
    let mut offsets = Vec::new();

    // 1. PDF Header
    pdf.extend_from_slice(b"%PDF-1.4\n%\x80\x80\x80\x80\n");

    // Object 1: Catalog
    offsets.push(pdf.len());
    pdf.extend_from_slice(b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    // Object 2: Pages
    offsets.push(pdf.len());
    pdf.extend_from_slice(b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");

    // Object 3: Page
    offsets.push(pdf.len());
    pdf.extend_from_slice(
        b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 64 64] /Resources << /XObject << /Im1 4 0 R >> >> /Contents 5 0 R >>\nendobj\n",
    );

    // Generate embedded JPEG stream for Object 4
    let jpeg_bytes = create_synthetic_jpeg_bytes(64, 64);

    // Object 4: Image XObject (JPEG)
    offsets.push(pdf.len());
    let obj4_header = format!(
        "4 0 obj\n<< /Type /XObject /Subtype /Image /Width 64 /Height 64 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length {} >>\nstream\r\n",
        jpeg_bytes.len()
    );
    pdf.extend_from_slice(obj4_header.as_bytes());
    pdf.extend_from_slice(&jpeg_bytes);
    pdf.extend_from_slice(b"\r\nendstream\nendobj\n");

    // Object 5: Content stream (draws image Im1)
    offsets.push(pdf.len());
    let content_bytes = b"q 64 0 0 64 0 0 cm /Im1 Do Q\n";
    let obj5_header = format!(
        "5 0 obj\n<< /Length {} >>\nstream\r\n",
        content_bytes.len()
    );
    pdf.extend_from_slice(obj5_header.as_bytes());
    pdf.extend_from_slice(content_bytes);
    pdf.extend_from_slice(b"\r\nendstream\nendobj\n");

    // Cross-reference table (xref)
    let startxref = pdf.len();
    let size = offsets.len() + 1; // Object 0 + 5 objects

    pdf.extend_from_slice(format!("xref\n0 {}\n", size).as_bytes());
    pdf.extend_from_slice(b"0000000000 65535 f \r\n");

    for offset in &offsets {
        pdf.extend_from_slice(format!("{:010} 00000 n \r\n", offset).as_bytes());
    }

    // Trailer
    let trailer = format!(
        "trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{}\n%%EOF\n",
        size, startxref
    );
    pdf.extend_from_slice(trailer.as_bytes());

    pdf
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
fn test_engine_construction() {
    let engine = VisionEngine::new();
    assert!(
        engine.is_ok(),
        "Failed to instantiate VisionEngine: {:?}",
        engine.err()
    );
}

#[test]
fn test_synthetic_inputs_and_jpeg_filtering() {
    let mut engine = VisionEngine::new().expect("Failed to construct VisionEngine");

    // 1. Test Non-JPEG Input (BMP) -> Transcode Path
    let synthetic_bmp = create_synthetic_bmp_bytes(64, 64);
    let bmp_result = engine.process_input(DocumentInput::RawImageBytes(&synthetic_bmp));
    assert!(
        bmp_result.is_ok(),
        "Non-JPEG (BMP) image processing failed: {:?}",
        bmp_result.err()
    );

    let bmp_buffer = bmp_result.unwrap();
    let (width, height, channels) = bmp_buffer.dimensions();
    assert!(width > 0 && height > 0 && channels > 0);

    // 2. Test Direct JPEG Input -> Zero-Copy Fast-Path
    let synthetic_jpeg = create_synthetic_jpeg_bytes(64, 64);
    let jpeg_result = engine.process_input(DocumentInput::RawImageBytes(&synthetic_jpeg));
    assert!(
        jpeg_result.is_ok(),
        "JPEG fast-path image processing failed: {:?}",
        jpeg_result.err()
    );

    let jpeg_buffer = jpeg_result.unwrap();
    if let Some(text) = jpeg_buffer.extracted_text() {
        println!("Generated/Extracted Text from native engine: {}", text);
    }

    // 3. Test Synthetic PDF Stream Input
    let synthetic_pdf = create_synthetic_pdf_bytes();
    let pdf_result = engine.process_input(DocumentInput::PdfDocumentBytes(&synthetic_pdf));
    assert!(
        pdf_result.is_ok(),
        "PDF visual processing failed: {:?}",
        pdf_result.err()
    );

    // 4. Test Synthetic Financial Chart Input with Custom Dimensions (1024x1024)
    let synthetic_chart = create_synthetic_bmp_bytes(1024, 1024);
    let chart_result = engine.process_input_with_dims(
        DocumentInput::FinancialChartBytes(&synthetic_chart),
        1024,
        1024,
        3,
    );
    assert!(
        chart_result.is_ok(),
        "Chart processing with custom dimensions failed: {:?}",
        chart_result.err()
    );
}
