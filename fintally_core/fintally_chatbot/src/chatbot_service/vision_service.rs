use anyhow::{bail, Context, Result};
use std::path::PathBuf;

use crate::native_layer::vision::OcrService;

#[derive(Debug, Clone)]
pub enum VisionAttachment {
    Image(Vec<u8>),
    Pdf(Vec<u8>),
    Chart(Vec<u8>),
}

pub struct VisionChatbotService {
    model_dir: Option<PathBuf>,
}

impl VisionChatbotService {
    pub fn new() -> Self {
        Self { model_dir: None }
    }

    pub fn with_model_dir<P: Into<PathBuf>>(model_dir: P) -> Self {
        Self {
            model_dir: Some(model_dir.into()),
        }
    }

    /// Process up to 3 attachments concurrently and aggregate extracted text.
    pub async fn process_attachments(
        &self,
        attachments: Vec<VisionAttachment>,
        prompt: String,
    ) -> Result<String> {
        // Enforce the strict 3 file maximum limit
        if attachments.len() > 3 {
            bail!("A maximum of 3 files can be processed at once.");
        }

        if attachments.is_empty() {
            return Ok(String::new());
        }

        let mut tasks = Vec::new();

        for (idx, attachment) in attachments.into_iter().enumerate() {
            let model_dir = self.model_dir.clone();
            let prompt_clone = prompt.clone();

            let task = tokio::task::spawn_blocking(move || -> Result<String> {
                let mut service = match model_dir {
                    Some(dir) => OcrService::from_model_dir(dir)?,
                    None => OcrService::new()?,
                };

                let res = match attachment {
                    VisionAttachment::Image(bytes) => service.process_image(&bytes, &prompt_clone)?,
                    VisionAttachment::Pdf(bytes) => service.process_pdf(&bytes, &prompt_clone)?,
                    VisionAttachment::Chart(bytes) => service.process_financial_chart(&bytes, &prompt_clone)?,
                };

                Ok(format!("--- FILE #{} EXTRACTED CONTEXT ---\n{}", idx + 1, res.trim()))
            });

            tasks.push(task);
        }

        let mut extracted_results = Vec::new();
        for task in tasks {
            let res = task
                .await
                .context("Tokio join error executing vision engine extraction")??;
            extracted_results.push(res);
        }

        Ok(extracted_results.join("\n\n"))
    }
}

impl Default for VisionChatbotService {
    fn default() -> Self {
        Self::new()
    }
}
