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

pub struct TokenOutputStream {
    tokenizer: Tokenizer,
    tokens: Vec<u32>,
    prev_index: usize,
    current_index: usize,
}

impl TokenOutputStream {
    #[inline]
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
    pub mmap: Arc<Mmap>,
    pub stop_token_ids: HashSet<u32>,
}

impl NativeLlamaEngine {
    pub fn load_from_vault(vault_dir: &str) -> Result<Self, String> {
        let path = PathBuf::from(vault_dir);
        let tokenizer_path = path.join("tokenizer.json");

        let device = select_device()?;

        let tokenizer = Tokenizer::from_file(&tokenizer_path)
            .map_err(|e| format!("Failed to parse tokenizer.json: {e}"))?;

        // Extract all Qwen special stop tokens
        let vocab = tokenizer.get_vocab(true);
        let mut stop_token_ids = HashSet::new();
        for &stop_str in &["<|endoftext|>", "<|im_end|>", "<|im_start|>"] {
            if let Some(&id) = vocab.get(stop_str) {
                stop_token_ids.insert(id);
            }
        }

        let gguf_path = Self::get_gguf_file_from_dir(&path).map_err(|e| e.to_string())?;

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
            stop_token_ids,
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

    /// Automatically formats raw text into standard Qwen2/2.5 ChatML template if missing.
    fn format_chatml_prompt(&self, prompt: &str) -> String {
        if prompt.contains("<|im_start|>") {
            prompt.to_string()
        } else {
            format!(
                "<|im_start|>system\nYou are a helpful, concise AI assistant.<|im_end|>\n<|im_start|>user\n{}<|im_end|>\n<|im_start|>assistant\n",
                prompt.trim()
            )
        }
    }

    fn create_request_model(&self) -> Result<ModelWeights, AppError> {
        let mut reader = Cursor::new(&self.mmap[..]);
        let content = gguf_file::Content::read(&mut reader)
            .map_err(|e| AppError::InferenceError(format!("Failed reading GGUF header: {e}")))?;

        ModelWeights::from_gguf(content, &mut reader, &self.device)
            .map_err(|e| AppError::InferenceError(format!("Failed instantiating GGUF model: {e}")))
    }

    /// Applies repetition penalty on a sliding window of recent tokens (prevents context corruption)
    #[inline]
    fn apply_windowed_repetition_penalty(logits: &mut [f32], penalty: f32, recent_tokens: &[u32]) {
        if (penalty - 1.0).abs() < f32::EPSILON || recent_tokens.is_empty() {
            return;
        }
        let unique_recent: HashSet<&u32> = recent_tokens.iter().collect();
        for &token_id in unique_recent {
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

        // 1. Format input using ChatML
        let formatted_prompt = self.format_chatml_prompt(prompt);

        let tokens = self
            .tokenizer
            .encode(formatted_prompt.as_str(), true)
            .map_err(|e| AppError::InferenceError(format!("Token encoding error: {e}")))?;

        let prompt_tokens = tokens.get_ids().to_vec();
        if prompt_tokens.is_empty() {
            return Err(AppError::InferenceError("Prompt cannot be empty".into()));
        }

        // Truncate incoming prompt tokens if they exceed 4096 to protect 6GB VRAM ceiling
        let prompt_tokens = if prompt_tokens.len() > 4096 {
            prompt_tokens[prompt_tokens.len() - 4096..].to_vec()
        } else {
            prompt_tokens
        };

        let mut model = self.create_request_model()?;
        let tokenizer_instance = self.tokenizer.clone();
        let compute_device = self.device.clone();
        let cancel_child = cancel.clone();
        let stop_tokens = self.stop_token_ids.clone();

        tokio::task::spawn_blocking(move || {
            if cancel_child.is_cancelled() {
                return;
            }

            let seed = rand::random::<u64>();

            // 2. Optimized sampling hyperparameters for quantized Qwen models:
            // Low temperature (0.5) and top_p (0.85) stabilize quantized weight noise.
            let mut logits_processor = LogitsProcessor::new(seed, Some(0.5), Some(0.85));
            let repetition_penalty: f32 = 1.05; // Reduced from 1.15 to prevent quality degradation
            const PENALTY_WINDOW: usize = 64;   // Restrict penalty to the last 64 generated tokens

            let mut token_stream = TokenOutputStream::new(tokenizer_instance);
            let mut recent_tokens: Vec<u32> = Vec::with_capacity(PENALTY_WINDOW);
            let mut generated_tokens = 0;

            let mut pos = 0;
            const TILE_CHUNK_SIZE: usize = 1024;
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

                // Filter non-finite logit values
                for v in logits_vec.iter_mut() {
                    if !v.is_finite() {
                        *v = -1e9;
                    }
                }

                // Apply windowed repetition penalty
                Self::apply_windowed_repetition_penalty(&mut logits_vec, repetition_penalty, &recent_tokens);

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

                // Stop evaluation on matched stop tokens
                if stop_tokens.contains(&next_token_id) {
                    break;
                }

                // Maintain window for repetition penalty
                if recent_tokens.len() >= PENALTY_WINDOW {
                    recent_tokens.remove(0);
                }
                recent_tokens.push(next_token_id);

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
