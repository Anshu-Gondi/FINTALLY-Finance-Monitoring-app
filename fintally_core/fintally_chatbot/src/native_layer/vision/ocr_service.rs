use std::path::Path;
use anyhow::{bail, Context, Result};
use unicode_normalization::UnicodeNormalization;

use crate::core::vision::engine::{DocumentInput, VisionEngine};
use crate::core::vision::ffi::FinThermalMetrics;

/// Cleans UTF-8 while preserving Markdown formatting, LaTeX, and table structure
fn sanitize_ocr_text(raw_text: &str) -> String {
    raw_text
        .nfkc()
        .collect::<String>()
        .lines()
        .map(|line| line.trim_end())
        .collect::<Vec<&str>>()
        .join("\n")
        .trim()
        .to_string()
}

pub struct OcrService {
    engine: VisionEngine,
}

impl OcrService {
    pub fn new() -> Result<Self> {
        let mut engine = VisionEngine::new()?;

        if let Err(err) = engine.warmup() {
            eprintln!("⚠️ Vision engine warmup completed with notice: {}", err);
        }

        Ok(Self { engine })
    }

    /// Backwards-compatible constructor pointing to `VisionEngine::new()`
    pub fn from_model_dir<P: AsRef<Path>>(_model_dir: P) -> Result<Self> {
        Self::new()
    }

    pub fn process_image(&mut self, image_bytes: &[u8], _prompt: &str) -> Result<String> {
        if image_bytes.is_empty() {
            bail!("Provided image byte buffer is empty");
        }

        let buffer = self
            .engine
            .process_input(DocumentInput::RawImageBytes(image_bytes))
            .context("Failed executing VisionEngine on raw image bytes")?;

        let text = buffer.extracted_text().unwrap_or_default();
        Ok(sanitize_ocr_text(text))
    }

    pub fn process_pdf(&mut self, pdf_bytes: &[u8], _prompt: &str) -> Result<String> {
        if pdf_bytes.is_empty() {
            bail!("Provided PDF byte buffer is empty");
        }

        let buffer = self
            .engine
            .process_input(DocumentInput::PdfDocumentBytes(pdf_bytes))
            .context("Failed executing VisionEngine on PDF document bytes")?;

        let text = buffer.extracted_text().unwrap_or_default();
        Ok(sanitize_ocr_text(text))
    }

    pub fn process_financial_chart(&mut self, chart_bytes: &[u8], _prompt: &str) -> Result<String> {
        if chart_bytes.is_empty() {
            bail!("Provided financial chart byte buffer is empty");
        }

        let buffer = self
            .engine
            .process_input(DocumentInput::FinancialChartBytes(chart_bytes))
            .context("Failed executing VisionEngine on financial chart bytes")?;

        let text = buffer.extracted_text().unwrap_or_default();
        Ok(sanitize_ocr_text(text))
    }

    pub fn process_custom_dimensions(
        &mut self,
        input: DocumentInput,
        _prompt: &str,
        width: usize,
        height: usize,
        channels: usize,
    ) -> Result<String> {
        let buffer = self
            .engine
            .process_input_with_dims(input, width, height, channels)
            .context("Failed executing VisionEngine with custom dimensions")?;

        let text = buffer.extracted_text().unwrap_or_default();
        Ok(sanitize_ocr_text(text))
    }

    pub fn get_thermal_metrics() -> FinThermalMetrics {
        VisionEngine::get_thermal_metrics()
    }

    pub fn set_thermal_thresholds(warm_limit_c: f32, critical_limit_c: f32) {
        VisionEngine::set_thermal_thresholds(warm_limit_c, critical_limit_c);
    }
}
