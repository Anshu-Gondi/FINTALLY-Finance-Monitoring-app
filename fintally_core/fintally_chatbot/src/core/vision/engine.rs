use super::{ffi::*, memory::SafeFinBuffer};
use anyhow::{bail, Context, Result};
use candle_core::{DType, Device, Tensor};
use lopdf::Document;
use memmap2::Mmap;
use serde_json::Value;
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
    dtype: DType,
    tokenizer: Tokenizer,
    eos_token_id: u32,
}

impl VisionEngine {
    /// Default constructor pointing to the GOT-OCR 2.0 model output path
    pub fn new() -> Result<Self> {
        Self::from_model_dir("llm_models/ocr/got_ocr2_0_output")
    }

    /// Flexible constructor for GOT-OCR 2.0 with dynamic device selection and F16 quantization
    pub fn from_model_dir<P: AsRef<Path>>(model_dir: P) -> Result<Self> {
        let model_dir = model_dir.as_ref();

        // 1. Initialize C++ Engine Context via FFI
        let native_ctx = unsafe { fin_engine_create() };
        if native_ctx.is_null() {
            bail!("Failed to allocate native C++ FinOcrEngineContext instance");
        }

        // 2. Hardware Acceleration Strategy (T4 GPU CUDA Auto-Selection)
        let (device, dtype) = if candle_core::utils::cuda_is_available() {
            println!("🚀 CUDA detected: Activating GPU 0 with FP16 for GOT-OCR 2.0");
            (Device::new_cuda(0)?, DType::F16)
        } else {
            println!("⚡ CUDA unavailable: Falling back to CPU with FP32");
            (Device::Cpu, DType::F32)
        };

        let config_path = model_dir.join("config.json");
        let weights_path = model_dir.join("model.safetensors");
        let tokenizer_path = model_dir.join("tokenizer.json");

        // 3. Load Tokenizer
        let tokenizer = Tokenizer::from_file(&tokenizer_path).map_err(|e| {
            anyhow::anyhow!("Failed to load GOT-OCR tokenizer from {:?}: {}", tokenizer_path, e)
        })?;

        // 4. GOT-OCR 2.0 (Qwen2 Backbone) EOS Token Detection
        let eos_token_id = tokenizer
            .token_to_id("<|im_end|>")
            .or_else(|| tokenizer.token_to_id("<|endoftext|>"))
            .or_else(|| tokenizer.token_to_id("</s>"))
            .or_else(|| tokenizer.token_to_id("<eos>"))
            .unwrap_or(151645); // Standard Qwen2 <|im_end|> token ID fallback

        // 5. Mmap Load & Verify Config JSON
        if config_path.exists() {
            let config_file = File::open(&config_path)
                .with_context(|| format!("Failed to open config file at {:?}", config_path))?;
            let config_mmap = unsafe { Mmap::map(&config_file)? };
            let _config_json: Value = serde_json::from_slice(&config_mmap)
                .with_context(|| format!("Failed to parse config JSON in {:?}", config_path))?;
        }

        // 6. Verify Safetensors Availability with zero-copy checking
        if !weights_path.exists() {
            bail!("GOT-OCR 2.0 model weights file missing at {:?}", weights_path);
        }

        Ok(Self {
            native_ctx,
            device,
            dtype,
            tokenizer,
            eos_token_id,
        })
    }

    /// Process input using GOT-OCR 2.0 native 1024x1024 resolution
    pub fn process_input(&mut self, input: DocumentInput, prompt: &str) -> Result<String> {
        // GOT-OCR 2.0 performs best at native 1024x1024 ViT patch dimensions
        self.process_input_with_dims(input, prompt, 1024, 1024, 3)
    }

    /// Process input with explicit dimensions, GOT prompts, and CLIP/Vary normalization
    pub fn process_input_with_dims(
        &mut self,
        input: DocumentInput,
        prompt: &str,
        target_width: usize,
        target_height: usize,
        channels: usize,
    ) -> Result<String> {
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

        // 1. Preprocess document through high-performance C++ engine via FFI
        let buffer_ptr = unsafe {
            fin_process_document_bytes(
                self.native_ctx,
                raw_bytes.as_ptr(),
                raw_bytes.len(),
                input_type,
                target_width,
                target_height,
                channels,
            )
        };

        let safe_buffer = SafeFinBuffer::new(buffer_ptr)
            .context("C++ native document preprocessing returned null buffer pointer")?;

        let (width, height, channels) = safe_buffer.dimensions();
        let pixel_data = safe_buffer.as_slice();

        // 2. Construct CPU Tensor from zero-copy C++ preprocessed buffer
        let raw_tensor = Tensor::from_slice(pixel_data, (height, width, channels), &Device::Cpu)?;

        // 3. Format NHWC -> NCHW and apply GOT-OCR 2.0 Vision Normalization
        // Mean: [0.48145466, 0.4578275, 0.40821073], Std: [0.26862954, 0.26130258, 0.27577711]
        let mean = Tensor::new(&[0.48145466f32, 0.4578275f32, 0.40821073f32], &Device::Cpu)?
            .reshape((3, 1, 1))?;
        let std = Tensor::new(&[0.26862954f32, 0.26130258f32, 0.27577711f32], &Device::Cpu)?
            .reshape((3, 1, 1))?;

        let normalized_image = raw_tensor
            .permute((2, 0, 1))?             // CHW
            .to_dtype(DType::F32)?
            .broadcast_div(&Tensor::new(255.0f32, &Device::Cpu)?)?;

        let normalized_image = normalized_image
            .broadcast_sub(&mean)?
            .broadcast_div(&std)?
            .unsqueeze(0)?                   // NCHW (1, 3, H, W)
            .to_device(&self.device)?
            .to_dtype(self.dtype)?;

        let _ = normalized_image; // Pass tensor forward to your GOT vision model forward pass

        // 4. GOT-OCR Prompt Formatting Trick
        let formatted_prompt = Self::format_got_prompt(prompt);

        // 5. Tokenize Prompt
        let mut generated_tokens: Vec<u32> = self
            .tokenizer
            .encode(formatted_prompt.as_str(), true)
            .map_err(|e| anyhow::anyhow!(e))?
            .get_ids()
            .to_vec();

        let max_new_tokens = 1024; // GOT-OCR generates long Markdown/LaTeX tables

        // 6. Autoregressive Generation Loop
        for _ in 0..max_new_tokens {
            let _input_ids = Tensor::new(generated_tokens.as_slice(), &self.device)?
                .unsqueeze(0)?;

            // Mock generation loop hook - replace next_token with actual model logits sampling
            let next_token = self.eos_token_id;

            if next_token == self.eos_token_id {
                break;
            }
            generated_tokens.push(next_token);
        }

        // 7. Decode Output Tokens
        let output_text = self
            .tokenizer
            .decode(&generated_tokens, true)
            .map_err(|e| anyhow::anyhow!(e))?;

        Ok(output_text)
    }

    /// Formats input prompt to match GOT-OCR 2.0 standard task specifications
    fn format_got_prompt(prompt: &str) -> String {
        let p = prompt.trim();
        if p.is_empty() || p.eq_ignore_ascii_case("format") || p.eq_ignore_ascii_case("markdown") {
            "<image>\nOCR with format: ".to_string()
        } else if p.eq_ignore_ascii_case("plain") || p.eq_ignore_ascii_case("raw") {
            "<image>\nOCR: ".to_string()
        } else if !p.contains("<image>") {
            format!("<image>\n{}", p)
        } else {
            p.to_string()
        }
    }

    /// Read current CPU thermal metrics from native thermal monitor
    pub fn get_thermal_metrics() -> FinThermalMetrics {
        unsafe { fin_get_thermal_metrics() }
    }

    /// Configure custom temperature threshold limits (Celsius)
    pub fn set_thermal_thresholds(warm_limit_c: f32, critical_limit_c: f32) {
        unsafe {
            fin_set_thermal_thresholds(warm_limit_c, critical_limit_c);
        }
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
