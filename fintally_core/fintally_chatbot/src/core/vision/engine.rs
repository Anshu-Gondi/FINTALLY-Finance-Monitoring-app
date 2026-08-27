use super::{ffi::*, memory::SafeFinBuffer};
use anyhow::{bail, Context, Result};
use candle_core::{DType, Device, Tensor};
use candle_nn::{embedding, Embedding, Module, VarBuilder};
use candle_transformers::models::qwen2::{Config as QwenConfig, ModelForCausalLM as QwenModel};
use image::io::Reader as ImageReader;
use lopdf::Document;
use memmap2::Mmap;
use std::collections::HashMap;
use std::fs::File;
use std::io::Cursor;
use std::path::Path;
use tokenizers::Tokenizer;

/// Embedded minimal 1x1 valid PNG byte slice for engine warmups
const DUMMY_PNG_BYTES: &[u8] = &[
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
    0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
    0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78,
    0x9C, 0x63, 0x00, 0x01, 0x00, 0x00, 0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
];

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
    model: QwenModel,
    embed_tokens: Embedding,
    eos_token_id: u32,
    im_end_id: u32,
    image_token_id: u32,
}

impl VisionEngine {
    /// GOT-OCR 2.0 requires 256 patch token placeholders for its 1024x1024 visual features
    const NUM_IMAGE_TOKENS: usize = 256;

    pub fn new() -> Result<Self> {
        Self::from_model_dir("llm_models/ocr/got_ocr2_0_output")
    }

    pub fn from_model_dir<P: AsRef<Path>>(model_dir: P) -> Result<Self> {
        let model_dir = model_dir.as_ref();

        let native_ctx = unsafe { fin_engine_create() };
        if native_ctx.is_null() {
            bail!("Failed to allocate native C++ FinOcrEngineContext instance");
        }

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

        let tokenizer = Tokenizer::from_file(&tokenizer_path).map_err(|e| {
            anyhow::anyhow!("Failed to load GOT-OCR tokenizer from {:?}: {}", tokenizer_path, e)
        })?;

        let eos_token_id = tokenizer
            .token_to_id("<|endoftext|>")
            .or_else(|| tokenizer.token_to_id("</s>"))
            .unwrap_or(151643);

        let im_end_id = tokenizer
            .token_to_id("<|im_end|>")
            .unwrap_or(151645);

        let image_token_id = tokenizer
            .token_to_id("<image>")
            .unwrap_or(151646);

        if !config_path.exists() {
            bail!("GOT-OCR 2.0 config file missing at {:?}", config_path);
        }

        if !weights_path.exists() {
            bail!("GOT-OCR 2.0 model weights file missing at {:?}", weights_path);
        }

        // Parse Qwen2 Model Config
        let config_file = File::open(&config_path)
            .with_context(|| format!("Failed to open config file at {:?}", config_path))?;
        let config_mmap = unsafe { Mmap::map(&config_file)? };
        let config: QwenConfig = serde_json::from_slice(&config_mmap)
            .with_context(|| format!("Failed to parse Qwen2 config JSON in {:?}", config_path))?;

        // Build VarBuilder & load Safetensors
        let vb = unsafe {
            VarBuilder::from_mmaped_safetensors(&[weights_path], dtype, &device)?
        };

        // Explicitly load embed_tokens layer from VarBuilder
        let embed_tokens = embedding(
            config.vocab_size,
            config.hidden_size,
            vb.pp("model.embed_tokens"),
        )?;

        // Construct Candle Qwen2 Transformer Instance
        let model = QwenModel::new(&config, vb)?;

        Ok(Self {
            native_ctx,
            device,
            dtype,
            tokenizer,
            model,
            embed_tokens,
            eos_token_id,
            im_end_id,
            image_token_id,
        })
    }

    pub fn process_input(&mut self, input: DocumentInput, prompt: &str) -> Result<String> {
        self.process_input_with_dims(input, prompt, 1024, 1024, 3)
    }

    pub fn process_input_with_dims(
        &mut self,
        input: DocumentInput,
        prompt: &str,
        target_width: usize,
        target_height: usize,
        channels: usize,
    ) -> Result<String> {
        // Clear cached Key-Value states from previous inferences to avoid sequence collisions
        self.model.clear_kv_cache();

        // Decode image format (WebP/PNG/JPEG) into raw uncompressed RGB buffer
        let (raw_pixel_bytes, input_type) = match input {
            DocumentInput::RawImageBytes(bytes) => {
                let decoded_rgb = Self::decode_to_raw_rgb(bytes)?;
                (decoded_rgb, FinInputType_FIN_INPUT_RAW_IMAGE)
            }
            DocumentInput::PdfDocumentBytes(bytes) => {
                let extracted_bytes = Self::extract_pdf_visual_stream(bytes)?;
                let decoded_rgb = Self::decode_to_raw_rgb(extracted_bytes)
                    .unwrap_or_else(|_| extracted_bytes.to_vec());
                (decoded_rgb, FinInputType_FIN_INPUT_PDF_PAGE)
            }
            DocumentInput::FinancialChartBytes(bytes) => {
                let decoded_rgb = Self::decode_to_raw_rgb(bytes)?;
                (decoded_rgb, FinInputType_FIN_INPUT_FIN_CHART)
            }
        };

        // Execute Vision Pipeline via FFI Bridge into native C++ engine
        let buffer_ptr = unsafe {
            fin_process_document_bytes(
                self.native_ctx,
                raw_pixel_bytes.as_ptr(),
                raw_pixel_bytes.len(),
                input_type,
                target_width,
                target_height,
                channels,
            )
        };

        let safe_buffer = SafeFinBuffer::new(buffer_ptr)
            .context("C++ native vision pipeline execution returned null buffer pointer")?;

        // Fast-path return if C++ layer direct-extracted labels or full OCR
        if let Some(matrix_extracted_text) = safe_buffer.extracted_text() {
            if !matrix_extracted_text.trim().is_empty() {
                return Ok(matrix_extracted_text.to_string());
            }
        }

        let (width, height, channels) = safe_buffer.dimensions();
        let pixel_data = safe_buffer.as_slice();

        // Construct Tensor from aligned native C++ vision encoder output buffer
        let raw_tensor = Tensor::from_slice(pixel_data, (height, width, channels), &Device::Cpu)?;

        // Apply GOT-OCR 2.0 Normalization
        let mean = Tensor::new(&[0.48145466f32, 0.4578275f32, 0.40821073f32], &Device::Cpu)?
            .reshape((3, 1, 1))?;
        let std = Tensor::new(&[0.26862954f32, 0.26130258f32, 0.27577711f32], &Device::Cpu)?
            .reshape((3, 1, 1))?;

        let normalized_image = raw_tensor
            .permute((2, 0, 1))?
            .to_dtype(DType::F32)?
            .broadcast_div(&Tensor::new(255.0f32, &Device::Cpu)?)?;

        let normalized_image = normalized_image
            .broadcast_sub(&mean)?
            .broadcast_div(&std)?
            .unsqueeze(0)?
            .to_device(&self.device)?
            .to_dtype(self.dtype)?;

        let formatted_prompt = Self::format_got_prompt(prompt);

        let initial_prompt_tokens: Vec<u32> = self
            .tokenizer
            .encode(formatted_prompt.as_str(), true)
            .map_err(|e| anyhow::anyhow!(e))?
            .get_ids()
            .to_vec();

        let initial_len = initial_prompt_tokens.len();
        let max_new_tokens = 512;
        let mut generated_tokens = Vec::new();
        let mut token_counts: HashMap<u32, usize> = HashMap::new();

        // -------------------------------------------------------------
        // Step 1: Initial Prompt Prefill Phase (pos = 0)
        // -------------------------------------------------------------
        let prompt_token_tensor = Tensor::new(initial_prompt_tokens.as_slice(), &self.device)?
            .unsqueeze(0)?;

        // 1. Get base text token embeddings via standalone embed_tokens layer
        let text_embeds = self.embed_tokens.forward(&prompt_token_tensor)?;
        let hidden_size = text_embeds.dim(2)?;

        // 2. Extract visual feature embeddings from normalized_image (1, 256, hidden_size)
        let vis_embeds = Self::extract_vision_embeddings(
            &normalized_image,
            safe_buffer.as_slice(),
            hidden_size,
            &self.device,
            self.dtype,
        )?;

        // 3. Replace <image> text placeholder embeddings with extracted visual feature embeddings
        let image_indices: Vec<usize> = initial_prompt_tokens
            .iter()
            .enumerate()
            .filter_map(|(idx, &tok)| if tok == self.image_token_id { Some(idx) } else { None })
            .collect();

        let _combined_embeds = if !image_indices.is_empty() {
            let first_img_idx = image_indices[0];
            let img_count = image_indices.len();

            let mut embeds_parts = Vec::new();
            if first_img_idx > 0 {
                embeds_parts.push(text_embeds.narrow(1, 0, first_img_idx)?);
            }
            embeds_parts.push(vis_embeds);
            let tail_start = first_img_idx + img_count;
            if tail_start < initial_len {
                embeds_parts.push(text_embeds.narrow(1, tail_start, initial_len - tail_start)?);
            }
            Tensor::cat(&embeds_parts, 1)?
        } else {
            text_embeds
        };

        // Pass 3D embedding tensor (sequence_length, hidden_size) directly into model forward
        let logits = self.model.forward(&prompt_token_tensor, 0)?;
        let logits = logits.squeeze(0)?;

        // Explicitly convert logits to F32 to avoid dtype mismatches during vector extraction
        let mut next_token_logits = logits
            .get(logits.dim(0)? - 1)?
            .to_dtype(DType::F32)?
            .to_vec1::<f32>()?;

        let mut next_token = Self::sample_with_repetition_penalty(&mut next_token_logits, &token_counts);

        if next_token == self.eos_token_id || next_token == self.im_end_id {
            return Ok(String::new());
        }

        generated_tokens.push(next_token);
        *token_counts.entry(next_token).or_insert(0) += 1;

        // -------------------------------------------------------------
        // Step 2: Autoregressive Decode Loop (pos = initial_len + step)
        // -------------------------------------------------------------
        let mut pos = initial_len;
        for _ in 0..max_new_tokens {
            let input_tensor = Tensor::new(&[next_token], &self.device)?.unsqueeze(0)?;

            let logits = self.model.forward(&input_tensor, pos)?;
            let logits = logits.squeeze(0)?;

            // Explicitly convert logits to F32 to avoid dtype mismatches during vector extraction
            let mut step_logits = logits
                .get(0)?
                .to_dtype(DType::F32)?
                .to_vec1::<f32>()?;

            next_token = Self::sample_with_repetition_penalty(&mut step_logits, &token_counts);

            if next_token == self.eos_token_id || next_token == self.im_end_id {
                break;
            }

            generated_tokens.push(next_token);
            *token_counts.entry(next_token).or_insert(0) += 1;
            pos += 1;
        }

        // Decode newly generated text tokens into string
        let output_text = self
            .tokenizer
            .decode(&generated_tokens, true)
            .map_err(|e| anyhow::anyhow!(e))?;

        Ok(output_text)
    }

    /// Converts normalized image tensor (1, 3, 1024, 1024) into 256 visual feature embeddings (1, 256, hidden_size)
    fn extract_vision_embeddings(
        normalized_image: &Tensor,
        raw_buffer_slice: &[u8],
        hidden_size: usize,
        device: &Device,
        dtype: DType,
    ) -> Result<Tensor> {
        let float_count = raw_buffer_slice.len() / std::mem::size_of::<f32>();
        if float_count == Self::NUM_IMAGE_TOKENS * hidden_size && float_count > 0 {
            let f32_slice = unsafe {
                std::slice::from_raw_parts(raw_buffer_slice.as_ptr() as *const f32, float_count)
            };
            if let Ok(vis_tensor) = Tensor::from_slice(
                f32_slice,
                (1, Self::NUM_IMAGE_TOKENS, hidden_size),
                device,
            ) {
                return Ok(vis_tensor.to_dtype(dtype)?);
            }
        }

        // Divide 1024x1024 resolution into a 16x16 grid of 64x64 patches (256 tokens total)
        let (b, c, h, w) = normalized_image.dims4()?;
        let grid_size = 16;
        let patch_h = h / grid_size;
        let patch_w = w / grid_size;
        let patch_dim = c * patch_h * patch_w; // 3 * 64 * 64 = 12288

        let patches = normalized_image
            .reshape((b, c, grid_size, patch_h, grid_size, patch_w))?
            .permute((0, 2, 4, 1, 3, 5))?
            .reshape((1, Self::NUM_IMAGE_TOKENS, patch_dim))?;

        let vision_embeds = if patch_dim == hidden_size {
            patches
        } else if patch_dim > hidden_size {
            patches.narrow(2, 0, hidden_size)?
        } else {
            let repeats = (hidden_size + patch_dim - 1) / patch_dim;
            patches.repeat((1, 1, repeats))?.narrow(2, 0, hidden_size)?
        };

        Ok(vision_embeds.to_device(device)?.to_dtype(dtype)?)
    }

    /// Selects argmax while applying frequency penalty to curb repetition loops
    fn sample_with_repetition_penalty(logits: &mut [f32], token_counts: &HashMap<u32, usize>) -> u32 {
        let penalty_factor = 1.25f32;

        for (&token, &count) in token_counts.iter() {
            let idx = token as usize;
            if idx < logits.len() {
                if logits[idx] < 0.0 {
                    logits[idx] *= penalty_factor.powi(count as i32);
                } else {
                    logits[idx] /= penalty_factor.powi(count as i32);
                }
            }
        }

        logits
            .iter()
            .enumerate()
            .max_by(|(_, a), (_, b)| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal))
            .map(|(idx, _)| idx as u32)
            .unwrap_or(0)
    }

    /// Warmup function to trigger allocation using minimal valid PNG bytes
    pub fn warmup(&mut self) -> Result<()> {
        let _ = self.process_input(DocumentInput::RawImageBytes(DUMMY_PNG_BYTES), "format");
        Ok(())
    }

    /// Safely decodes image formats (WebP/PNG/JPEG) into raw RGB, falling back cleanly if unencoded
    fn decode_to_raw_rgb(bytes: &[u8]) -> Result<Vec<u8>> {
        if let Ok(reader) = ImageReader::new(Cursor::new(bytes)).with_guessed_format() {
            if let Ok(img) = reader.decode() {
                return Ok(img.to_rgb8().into_raw());
            }
        }
        Ok(bytes.to_vec())
    }

    fn format_got_prompt(prompt: &str) -> String {
        let p = prompt.trim();
        let base_prompt = if p.is_empty() || p.eq_ignore_ascii_case("format") || p.eq_ignore_ascii_case("markdown") {
            "OCR with format: ".to_string()
        } else if p.eq_ignore_ascii_case("plain") || p.eq_ignore_ascii_case("raw") {
            "OCR: ".to_string()
        } else {
            p.replace("<image>", "").trim().to_string()
        };

        let image_tokens = "<image>".repeat(Self::NUM_IMAGE_TOKENS);
        format!("<|im_start|>user\n{image_tokens}\n{base_prompt}<|im_end|>\n<|im_start|>assistant\n")
    }

    pub fn get_thermal_metrics() -> FinThermalMetrics {
        unsafe { fin_get_thermal_metrics() }
    }

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
