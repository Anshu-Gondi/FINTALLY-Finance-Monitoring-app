use std::path::Path;
use anyhow::{bail, Context, Result};
use unicode_normalization::UnicodeNormalization;

use crate::core::vision::engine::{DocumentInput, VisionEngine};
use crate::core::vision::ffi::FinThermalMetrics;

/// Cleans and normalizes OCR string output into standard UTF-8 for LLM consumption
fn sanitize_ocr_text(raw_text: &str) -> String {
    raw_text
        .nfkc()
        .filter(|c| {
            c.is_alphanumeric()
                || c.is_ascii_punctuation()
                || *c == ' '
                || *c == '\n'
                || *c == '\t'
        })
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<&str>>()
        .join(" ")
}

/// Lock-free high-level OCR Service wrapping `VisionEngine`
pub struct OcrService {
    engine: VisionEngine,
}

impl OcrService {
    /// Default constructor pointing to standard GOT-OCR 2.0 output path
    pub fn new() -> Result<Self> {
        let engine = VisionEngine::new()?;
        Ok(Self { engine })
    }

    /// Custom path constructor
    pub fn from_model_dir<P: AsRef<Path>>(model_dir: P) -> Result<Self> {
        let engine = VisionEngine::from_model_dir(model_dir)?;
        Ok(Self { engine })
    }

    /// Process raw image binary payload
    pub fn process_image(&mut self, image_bytes: &[u8], prompt: &str) -> Result<String> {
        if image_bytes.is_empty() {
            bail!("Provided image byte buffer is empty");
        }

        let raw_text = self
            .engine
            .process_input(DocumentInput::RawImageBytes(image_bytes), prompt)
            .context("Failed executing VisionEngine on raw image bytes")?;

        Ok(sanitize_ocr_text(&raw_text))
    }

    /// Process PDF document binary payload
    pub fn process_pdf(&mut self, pdf_bytes: &[u8], prompt: &str) -> Result<String> {
        if pdf_bytes.is_empty() {
            bail!("Provided PDF byte buffer is empty");
        }

        let raw_text = self
            .engine
            .process_input(DocumentInput::PdfDocumentBytes(pdf_bytes), prompt)
            .context("Failed executing VisionEngine on PDF document bytes")?;

        Ok(sanitize_ocr_text(&raw_text))
    }

    /// Process financial chart visual data
    pub fn process_financial_chart(&mut self, chart_bytes: &[u8], prompt: &str) -> Result<String> {
        if chart_bytes.is_empty() {
            bail!("Provided financial chart byte buffer is empty");
        }

        let raw_text = self
            .engine
            .process_input(DocumentInput::FinancialChartBytes(chart_bytes), prompt)
            .context("Failed executing VisionEngine on financial chart bytes")?;

        Ok(sanitize_ocr_text(&raw_text))
    }

    /// Process with explicit custom width, height, and channels
    pub fn process_custom_dimensions(
        &mut self,
        input: DocumentInput,
        prompt: &str,
        width: usize,
        height: usize,
        channels: usize,
    ) -> Result<String> {
        let raw_text = self
            .engine
            .process_input_with_dims(input, prompt, width, height, channels)
            .context("Failed executing VisionEngine with custom dimensions")?;

        Ok(sanitize_ocr_text(&raw_text))
    }

    /// Direct thermal readings from CPU hardware sensor
    pub fn get_thermal_metrics() -> FinThermalMetrics {
        VisionEngine::get_thermal_metrics()
    }

    /// Set dynamic thermal throttling thresholds
    pub fn set_thermal_thresholds(warm_limit_c: f32, critical_limit_c: f32) {
        VisionEngine::set_thermal_thresholds(warm_limit_c, critical_limit_c);
    }
}
