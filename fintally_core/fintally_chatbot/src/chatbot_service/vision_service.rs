use anyhow::{Context, Result};
use std::path::PathBuf;

use crate::core::vision::engine::DocumentInput;
use crate::native_layer::vision::OcrService;

pub struct VisionChatbotService {
    model_dir: Option<PathBuf>,
}

impl VisionChatbotService {
    /// Creates a service pointing to default model paths
    pub fn new() -> Self {
        Self { model_dir: None }
    }

    /// Creates a service pointing to a custom model directory
    pub fn with_model_dir<P: Into<PathBuf>>(model_dir: P) -> Self {
        Self {
            model_dir: Some(model_dir.into()),
        }
    }

    /// Internal helper to instantiate an OcrService instance inside blocking context
    fn init_engine(&self) -> Result<OcrService> {
        match &self.model_dir {
            Some(dir) => OcrService::from_model_dir(dir),
            None => OcrService::new(),
        }
    }

    /// Process raw image byte stream for chat prompt enrichment
    pub async fn process_image_payload(
        &self,
        image_bytes: Vec<u8>,
        prompt: String,
    ) -> Result<String> {
        let model_dir = self.model_dir.clone();

        tokio::task::spawn_blocking(move || {
            let mut service = match model_dir {
                Some(dir) => OcrService::from_model_dir(dir)?,
                None => OcrService::new()?,
            };
            service
                .process_image(&image_bytes, &prompt)
                .context("Failed executing image OCR task")
        })
        .await
        .context("Tokio thread join fault during image OCR execution")?
    }

    /// Process PDF document payload for chat prompt enrichment
    pub async fn process_pdf_payload(
        &self,
        pdf_bytes: Vec<u8>,
        prompt: String,
    ) -> Result<String> {
        let model_dir = self.model_dir.clone();

        tokio::task::spawn_blocking(move || {
            let mut service = match model_dir {
                Some(dir) => OcrService::from_model_dir(dir)?,
                None => OcrService::new()?,
            };
            service
                .process_pdf(&pdf_bytes, &prompt)
                .context("Failed executing PDF document OCR task")
        })
        .await
        .context("Tokio thread join fault during PDF OCR execution")?
    }

    /// Process financial charts for analytical prompt generation
    pub async fn process_chart_payload(
        &self,
        chart_bytes: Vec<u8>,
        prompt: String,
    ) -> Result<String> {
        let model_dir = self.model_dir.clone();

        tokio::task::spawn_blocking(move || {
            let mut service = match model_dir {
                Some(dir) => OcrService::from_model_dir(dir)?,
                None => OcrService::new()?,
            };
            service
                .process_financial_chart(&chart_bytes, &prompt)
                .context("Failed executing financial chart OCR task")
        })
        .await
        .context("Tokio thread join fault during chart OCR execution")?
    }
}

impl Default for VisionChatbotService {
    fn default() -> Self {
        Self::new()
    }
}
