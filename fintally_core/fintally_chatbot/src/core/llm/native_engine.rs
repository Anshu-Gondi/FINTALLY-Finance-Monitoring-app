use crate::core::llm::engine::{CancelableStream, LlmEngine};
use crate::core::utils::errors::AppError;

use async_trait::async_trait;
use std::collections::HashSet;
use std::fs::File;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use tokio::sync::mpsc;
use tokio_stream::wrappers::ReceiverStream;
use tokio_util::sync::CancellationToken;

use candle_core::quantized::gguf_file;
use candle_core::{DType, Device, Tensor};
use candle_transformers::generation::LogitsProcessor;
use candle_transformers::models::quantized_qwen2::ModelWeights;
use memmap2::{Mmap, MmapOptions};
use tokenizers::Tokenizer;

/// Helper struct for streaming decoded tokens without splitting multi-byte UTF-8 sequences.
pub struct TokenOutputStream {
    tokenizer: Tokenizer,
    tokens: Vec<u32>,
    prev_index: usize,
    current_index: usize,
}

impl TokenOutputStream {
    pub fn new(tokenizer: Tokenizer) -> Self {
        Self {
            tokenizer,
            tokens: Vec::new(),
            prev_index: 0,
            current_index: 0,
        }
    }

    pub fn next_token(&mut self, token: u32) -> Result<Option<String>, AppError> {
        let prev_text = if self.tokens.is_empty() {
            String::new()
        } else {
            let tokens = &self.tokens[self.prev_index..self.current_index];
            self.tokenizer
                .decode(tokens, true)
                .map_err(|e| AppError::InferenceError(e.to_string()))?
        };
        self.tokens.push(token);
        let text = self
            .tokenizer
            .decode(&self.tokens[self.prev_index..], true)
            .map_err(|e| AppError::InferenceError(e.to_string()))?;

        if text.len() > prev_text.len() && !text.ends_with('\u{FFFD}') {
            let text = text[prev_text.len()..].to_string();
            self.prev_index = self.current_index;
            self.current_index = self.tokens.len();
            Ok(Some(text))
        } else {
            Ok(None)
        }
    }

    pub fn decode_rest(&self) -> Result<Option<String>, AppError> {
        let prev_text = if self.tokens.is_empty() {
            String::new()
        } else {
            let tokens = &self.tokens[self.prev_index..self.current_index];
            self.tokenizer
                .decode(tokens, true)
                .map_err(|e| AppError::InferenceError(e.to_string()))?
        };
        let text = self
            .tokenizer
            .decode(&self.tokens[self.prev_index..], true)
            .map_err(|e| AppError::InferenceError(e.to_string()))?;
        if text.len() > prev_text.len() {
            Ok(Some(text[prev_text.len()..].to_string()))
        } else {
            Ok(None)
        }
    }
}

fn select_device() -> Result<Device, String> {
    if candle_core::utils::cuda_is_available() {
        println!("🚀 CUDA GPU detected!");
        Device::new_cuda(0).map_err(|e| format!("Failed GPU 0: {e}"))
    } else {
        println!("⚠️ Running Candle on CPU.");
        Ok(Device::Cpu)
    }
}

pub struct NativeLlamaEngine {
    pub vault_dir: PathBuf,
    pub tokenizer: Tokenizer,
    pub device: Device,
    // Shared memory map across requests
    pub mmap: Arc<Mmap>,
    // Cached special token IDs to prevent repeated dictionary lookups
    pub eos_token_id: Option<u32>,
    pub im_end_token_id: Option<u32>,
}

impl NativeLlamaEngine {
    pub fn load_from_vault(vault_dir: &str) -> Result<Self, String> {
        let path = PathBuf::from(vault_dir);
        let tokenizer_path = path.join("tokenizer.json");

        let device = select_device()?;

        let tokenizer = Tokenizer::from_file(&tokenizer_path)
            .map_err(|e| format!("Failed to parse tokenizer.json: {e}"))?;

        // Cache special EOS token IDs once
        let vocab = tokenizer.get_vocab(true);
        let eos_token_id = vocab.get("<|endoftext|>").copied();
        let im_end_token_id = vocab.get("<|im_end|>").copied();

        // Locate GGUF File
        let gguf_path = Self::get_gguf_file_from_dir(&path).map_err(|e| e.to_string())?;

        // Open and Mmap the GGUF model file ONCE
        let file = File::open(&gguf_path)
            .map_err(|e| format!("Failed opening GGUF file {:?}: {e}", gguf_path))?;

        let mmap = unsafe {
            MmapOptions::new()
                .map(&file)
                .map_err(|e| format!("Failed to mmap GGUF file {:?}: {e}", gguf_path))?
        };

        Ok(Self {
            vault_dir: path,
            tokenizer,
            device,
            mmap: Arc::new(mmap),
            eos_token_id,
            im_end_token_id,
        })
    }

    fn get_gguf_file_from_dir(vault_dir: &Path) -> Result<PathBuf, AppError> {
        if let Ok(entries) = std::fs::read_dir(vault_dir) {
            for entry in entries.flatten() {
                let p = entry.path();
                if p.extension().and_then(|s| s.to_str()) == Some("gguf") {
                    return Ok(p);
                }
            }
        }
        Err(AppError::InferenceError(format!(
            "No .gguf files found in vault path: {:?}",
            vault_dir
        )))
    }

    /// Creates request-local ModelWeights by parsing GGUF content on demand from memory
    fn create_request_model(&self) -> Result<ModelWeights, AppError> {
        let mut reader = Cursor::new(&self.mmap[..]);
        let gguf_content = gguf_file::Content::read(&mut reader)
            .map_err(|e| AppError::InferenceError(format!("Failed parsing GGUF metadata: {e}")))?;

        ModelWeights::from_gguf(gguf_content, &mut reader, &self.device)
            .map_err(|e| AppError::InferenceError(format!("Failed instantiating GGUF model: {e}")))
    }

    #[inline]
    fn apply_repetition_penalty(logits: &mut [f32], penalty: f32, seen_tokens: &HashSet<u32>) {
        if penalty == 1.0 || seen_tokens.is_empty() {
            return;
        }
        for &token_id in seen_tokens {
            if let Some(logit) = logits.get_mut(token_id as usize) {
                if *logit < 0.0 {
                    *logit *= penalty;
                } else {
                    *logit /= penalty;
                }
            }
        }
    }
}

#[async_trait]
impl LlmEngine for NativeLlamaEngine {
    async fn generate(&self, prompt: &str, max_tokens: usize) -> Result<String, AppError> {
        let mut cancelable = self.stream_generate(prompt, max_tokens).await?;
        let mut out = String::new();

        use futures_util::StreamExt;
        while let Some(chunk) = cancelable.stream.next().await {
            out.push_str(&chunk?);
        }
        Ok(out)
    }

    async fn stream_generate(
        &self,
        prompt: &str,
        max_tokens: usize,
    ) -> Result<CancelableStream, AppError> {
        let (tx, rx) = mpsc::channel::<Result<String, AppError>>(32);
        let cancel = CancellationToken::new();

        let tokens = self
            .tokenizer
            .encode(prompt.trim(), true)
            .map_err(|e| AppError::InferenceError(format!("Token encoding error: {e}")))?;

        let prompt_tokens = tokens.get_ids().to_vec();
        if prompt_tokens.is_empty() {
            return Err(AppError::InferenceError("Prompt cannot be empty".into()));
        }

        let mut model = self.create_request_model()?;
        let tokenizer_instance = self.tokenizer.clone();
        let compute_device = self.device.clone();
        let cancel_child = cancel.clone();
        let eos_id = self.eos_token_id;
        let im_end_id = self.im_end_token_id;

        tokio::task::spawn_blocking(move || {
            if cancel_child.is_cancelled() {
                return;
            }

            let seed = rand::random::<u64>();
            let mut logits_processor = LogitsProcessor::new(seed, Some(0.7), Some(0.8));
            let repetition_penalty: f32 = 1.15;

            // Stream decoder handles multi-byte UTF-8 token boundaries safely
            let mut token_stream = TokenOutputStream::new(tokenizer_instance);
            let mut seen_tokens: HashSet<u32> = HashSet::with_capacity(max_tokens);
            let mut generated_tokens = 0;

            let mut pos = 0;
            const TILE_CHUNK_SIZE: usize = 256;
            let mut last_logits: Option<Tensor> = None;

            // ── STAGE 1: Tiled Prefill Pass ──
            let total_prompt_len = prompt_tokens.len();
            let mut offset = 0;

            while offset < total_prompt_len && !cancel_child.is_cancelled() {
                let chunk_len = usize::min(TILE_CHUNK_SIZE, total_prompt_len - offset);
                let chunk_tokens = &prompt_tokens[offset..offset + chunk_len];

                let input_tensor = match Tensor::from_slice(chunk_tokens, (1, chunk_len), &compute_device) {
                    Ok(t) => t,
                    Err(e) => {
                        let _ = tx.blocking_send(Err(AppError::InferenceError(e.to_string())));
                        return;
                    }
                };

                let logits = match model.forward(&input_tensor, pos) {
                    Ok(l) => l,
                    Err(e) => {
                        let _ = tx.blocking_send(Err(AppError::InferenceError(format!("Forward pass error: {e}"))));
                        return;
                    }
                };

                pos += chunk_len;
                offset += chunk_len;
                last_logits = Some(logits);
            }

            let mut logits = match last_logits {
                Some(l) => l,
                None => {
                    let _ = tx.blocking_send(Err(AppError::InferenceError("Empty prompt tokens".into())));
                    return;
                }
            };

            // ── STAGE 2: Autoregressive Token Generation ──
            while generated_tokens < max_tokens && !cancel_child.is_cancelled() {
                let last_logit = if logits.rank() == 2 {
                    match logits.squeeze(0) {
                        Ok(s) => s,
                        Err(e) => {
                            let _ = tx.blocking_send(Err(AppError::InferenceError(e.to_string())));
                            break;
                        }
                    }
                } else {
                    logits
                };

                let vocab_size = last_logit.dim(0).unwrap_or(151936);

                let mut logits_vec = match last_logit.to_dtype(DType::F32) {
                    Ok(t) => match t.to_vec1::<f32>() {
                        Ok(v) => v,
                        Err(e) => {
                            let _ = tx.blocking_send(Err(AppError::InferenceError(e.to_string())));
                            break;
                        }
                    },
                    Err(e) => {
                        let _ = tx.blocking_send(Err(AppError::InferenceError(e.to_string())));
                        break;
                    }
                };

                // Sanitize NaNs / Inf values
                for v in logits_vec.iter_mut() {
                    if !v.is_finite() {
                        *v = -1e9;
                    }
                }

                Self::apply_repetition_penalty(&mut logits_vec, repetition_penalty, &seen_tokens);

                let mut logits_tensor = match Tensor::from_vec(logits_vec, (vocab_size,), &Device::Cpu) {
                    Ok(t) => t,
                    Err(e) => {
                        let _ = tx.blocking_send(Err(AppError::InferenceError(e.to_string())));
                        break;
                    }
                };

                let next_token_id = match logits_processor.sample(&mut logits_tensor) {
                    Ok(id) => id,
                    Err(e) => {
                        let _ = tx.blocking_send(Err(AppError::InferenceError(e.to_string())));
                        break;
                    }
                };

                if Some(next_token_id) == eos_id || Some(next_token_id) == im_end_id {
                    break;
                }

                seen_tokens.insert(next_token_id);

                // Stream decoded text chunk safely via TokenOutputStream
                if let Ok(Some(token_str)) = token_stream.next_token(next_token_id) {
                    if tx.blocking_send(Ok(token_str)).is_err() {
                        break;
                    }
                }

                let input_tensor = match Tensor::from_slice(&[next_token_id], (1, 1), &compute_device) {
                    Ok(t) => t,
                    Err(e) => {
                        let _ = tx.blocking_send(Err(AppError::InferenceError(e.to_string())));
                        break;
                    }
                };

                logits = match model.forward(&input_tensor, pos) {
                    Ok(l) => l,
                    Err(e) => {
                        let _ = tx.blocking_send(Err(AppError::InferenceError(e.to_string())));
                        break;
                    }
                };

                pos += 1;
                generated_tokens += 1;
            }

            // Flush remaining buffered stream bytes if any
            if let Ok(Some(rest)) = token_stream.decode_rest() {
                let _ = tx.blocking_send(Ok(rest));
            }
        });

        Ok(CancelableStream {
            stream: Box::pin(ReceiverStream::new(rx)),
            cancel,
        })
    }

    async fn embed(&self, _text: &str) -> Result<Vec<f32>, AppError> {
        Ok(vec![0.0f32; 384])
    }
}
