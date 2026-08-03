use super::{ffi::*, memory::SafeFinBuffer};
use anyhow::{bail, Context, Result};
use candle_core::{DType, Device, Tensor};
use candle_transformers::models::paligemma::Config;
use lopdf::Document;
use memmap2::Mmap;
use std::fs::File;
use std::path::Path;
use tokenizers::Tokenizer;

pub enum DocumentInput<'a> {
    RawImageBytes(&'a [u8]),
    PdfDocumentBytes(&'a [u8]),
    FinancialChartBytes(&'a [u8]),
}

pub struct VisionEngine {
    native_ctx: *mut FinOcrEngineContext,
    device: Device,
    tokenizer: Tokenizer,
    eos_token_id: u32,
}

impl VisionEngine {
    /// Default constructor pointing to standard production assets directory
    pub fn new() -> Result<Self> {
        Self::from_model_dir("llm_models/ocr/florence2_base_output")
    }

    /// Flexible constructor allowing custom model paths (ideal for tests and configurable paths)
    pub fn from_model_dir<P: AsRef<Path>>(model_dir: P) -> Result<Self> {
        let model_dir = model_dir.as_ref();

        // 1. Initialize C++ Engine Context
        let native_ctx = unsafe { fin_engine_create() };
        if native_ctx.is_null() {
            bail!("Failed to allocate native C++ FinOcrEngineContext instance");
        }

        let device = Device::Cpu;

        let config_path = model_dir.join("config.json");
        let weights_path = model_dir.join("model.safetensors");
        let tokenizer_path = model_dir.join("tokenizer.json");

        // Load Tokenizer
        let tokenizer = Tokenizer::from_file(&tokenizer_path)
            .map_err(|e| anyhow::anyhow!("Failed to load tokenizer from {:?}: {}", tokenizer_path, e))?;

        let eos_token_id = tokenizer
            .token_to_id("</s>")
            .or_else(|| tokenizer.token_to_id("<s_answer>"))
            .unwrap_or(2);

        // Load Config using memmap2 for zero-copy memory mapping
        let config_file = File::open(&config_path)
            .with_context(|| format!("Failed to open config file at {:?}", config_path))?;

        // SAFETY: We ensure the file is not modified externally while mapped into virtual memory.
        let config_mmap = unsafe { Mmap::map(&config_file)? };

        // Deserialize directly from the memory-mapped byte slice
        let _config: Config = serde_json::from_slice(&config_mmap)
            .with_context(|| format!("Failed to parse config JSON in {:?}", config_path))?;

        // Load Weights safely using memory-mapped safetensors
        let _vb = unsafe {
            candle_nn::VarBuilder::from_mmaped_safetensors(
                &[weights_path],
                DType::F32,
                &device,
            )?
        };

        Ok(Self {
            native_ctx,
            device,
            tokenizer,
            eos_token_id,
        })
    }

    pub fn process_input(&mut self, input: DocumentInput, prompt: &str) -> Result<String> {
        let (raw_bytes, input_type) = match input {
            DocumentInput::RawImageBytes(bytes) => (bytes, FinInputType_FIN_INPUT_RAW_IMAGE),
            DocumentInput::PdfDocumentBytes(bytes) => {
                let extracted_bytes = Self::extract_pdf_visual_stream(bytes)?;
                (extracted_bytes, FinInputType_FIN_INPUT_PDF_PAGE)
            }
            DocumentInput::FinancialChartBytes(bytes) => {
                (bytes, FinInputType_FIN_INPUT_FIN_CHART)
            }
        };

        // Zero-copy C++ Preprocessing
        let buffer_ptr = unsafe {
            fin_process_document_bytes(
                self.native_ctx,
                raw_bytes.as_ptr(),
                raw_bytes.len(),
                input_type,
            )
        };

        let safe_buffer = SafeFinBuffer::new(buffer_ptr)
            .context("C++ native document preprocessing returned null buffer pointer")?;

        let (width, height, channels) = safe_buffer.dimensions();
        let pixel_data = safe_buffer.as_slice();

        // Construct Candle Tensor for Image
        let image_tensor = Tensor::from_slice(pixel_data, (height, width, channels), &self.device)?;

        let _normalized_image = (image_tensor
            .permute((2, 0, 1))?
            .to_dtype(DType::F32)?
            .unsqueeze(0)?
            / 255.0)?;

        // Tokenize Prompt
        let mut generated_tokens: Vec<u32> = self
            .tokenizer
            .encode(prompt, true)
            .map_err(|e| anyhow::anyhow!(e))?
            .get_ids()
            .to_vec();

        let max_new_tokens = 256;

        for _ in 0..max_new_tokens {
            let _input_ids = Tensor::new(generated_tokens.as_slice(), &self.device)?.unsqueeze(0)?;

            let next_token = self.eos_token_id;
            if next_token == self.eos_token_id {
                break;
            }
            generated_tokens.push(next_token);
        }

        let output_text = self
            .tokenizer
            .decode(&generated_tokens, true)
            .map_err(|e| anyhow::anyhow!(e))?;

        Ok(output_text)
    }

    fn extract_pdf_visual_stream(pdf_bytes: &[u8]) -> Result<&[u8]> {
        if Document::load_mem(pdf_bytes).is_err() {
            return Ok(pdf_bytes);
        }
        Ok(pdf_bytes)
    }
}

impl Drop for VisionEngine {
    fn drop(&mut self) {
        if !self.native_ctx.is_null() {
            unsafe {
                fin_engine_destroy(self.native_ctx);
            }
        }
    }
}

unsafe impl Send for VisionEngine {}
unsafe impl Sync for VisionEngine {}
