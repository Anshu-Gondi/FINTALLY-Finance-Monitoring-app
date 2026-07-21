use crate::core::llm::engine::{CancelableStream, LlmEngine};
use crate::core::utils::errors::AppError;

use async_trait::async_trait;
use std::collections::HashSet;
use std::path::PathBuf;
use tokio::sync::mpsc;
use tokio_stream::wrappers::ReceiverStream;
use tokio_util::sync::CancellationToken;

use candle_core::{DType, Device, Tensor};
use candle_transformers::generation::LogitsProcessor;
use candle_transformers::models::qwen2::{Config, Model};
use tokenizers::Tokenizer;

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
    pub model_config: Config,
    pub vault_dir: PathBuf,
    pub tokenizer: Tokenizer,
    pub device: Device,
    pub dtype: DType,
}

impl NativeLlamaEngine {
    pub fn load_from_vault(vault_dir: &str) -> Result<Self, String> {
        let path = PathBuf::from(vault_dir);
        let config_path = path.join("config.json");
        let tokenizer_path = path.join("tokenizer.json");

        let device = select_device()?;

        let config_str = std::fs::read_to_string(&config_path)
            .map_err(|e| format!("Failed to read config.json: {e}"))?;
        let config: Config = serde_json::from_str(&config_str)
            .map_err(|e| format!("Malformed config payload: {e}"))?;

        let tokenizer = Tokenizer::from_file(&tokenizer_path)
            .map_err(|e| format!("Failed to parse tokenizer.json: {e}"))?;

        let dtype = if device.is_cuda() {
            DType::BF16
        } else {
            DType::F32
        };

        Ok(Self {
            model_config: config,
            vault_dir: path,
            tokenizer,
            device,
            dtype,
        })
    }

    /// Recursively find all `.safetensors` files in directory to ensure full model loading
    fn get_weight_files(&self) -> Result<Vec<PathBuf>, AppError> {
        let mut st_files = Vec::new();
        if let Ok(entries) = std::fs::read_dir(&self.vault_dir) {
            for entry in entries.flatten() {
                let p = entry.path();
                if p.extension().and_then(|s| s.to_str()) == Some("safetensors") {
                    st_files.push(p);
                }
            }
        }
        if st_files.is_empty() {
            return Err(AppError::InferenceError(format!(
                "No .safetensors files found in vault path: {:?}",
                self.vault_dir
            )));
        }
        st_files.sort();
        Ok(st_files)
    }

    fn instantiate_model(&self) -> Result<Model, AppError> {
        let weight_files = self.get_weight_files()?;
        let vb = unsafe {
            candle_nn::VarBuilder::from_mmaped_safetensors(&weight_files, self.dtype, &self.device)
                .map_err(|e| AppError::InferenceError(format!("VarBuilder error: {e}")))?
        };

        Model::new(&self.model_config, vb)
            .map_err(|e| AppError::InferenceError(format!("Model construction error: {e}")))
    }

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

        // FIXED: `prompt` is expected to ALREADY be a fully-formatted ChatML string
        // (built by the caller, e.g. ChatbotOrchestrator::build_chat_template).
        // We do NOT re-wrap it in another system/user/assistant turn anymore —
        // doing so previously nested one malformed prompt inside another,
        // which confused the model into repetitive/incoherent output.
        let tokens = self
            .tokenizer
            .encode(prompt.trim(), true)
            .map_err(|e| AppError::InferenceError(format!("Token encoding error: {e}")))?;

        let prompt_tokens = tokens.get_ids().to_vec();

        let mut model = self.instantiate_model()?;
        let tokenizer_instance = self.tokenizer.clone();
        let compute_device = self.device.clone();
        let cancel_child = cancel.clone();

        tokio::task::spawn_blocking(move || {
            if cancel_child.is_cancelled() {
                return;
            }

            let seed = rand::random::<u64>();
            let mut logits_processor = LogitsProcessor::new(seed, Some(0.7), Some(0.8));
            let repetition_penalty: f32 = 1.15;

            // FIXED: do NOT seed this with prompt_tokens. Seeding with the entire
            // prompt (which can be hundreds/thousands of tokens: system prompt,
            // RAG context, chat history, etc.) caused the repetition penalty to
            // suppress almost every commonly-used token in the vocabulary before
            // generation even started, forcing the model to sample from a tiny
            // leftover set of tokens and loop on them (e.g. "byby by by").
            // This set should only ever contain tokens the model itself generates.
            let mut seen_tokens: HashSet<u32> = HashSet::new();
            let mut generated_tokens = 0;

            let mut pos = 0;
            let mut input_tokens = prompt_tokens.clone();

            let eos_id = tokenizer_instance.get_vocab(true).get("<|endoftext|>").copied();
            let im_end_id = tokenizer_instance.get_vocab(true).get("<|im_end|>").copied();

            while generated_tokens < max_tokens && !cancel_child.is_cancelled() {
                let context_len = input_tokens.len();

                let input_tensor = match Tensor::new(input_tokens.as_slice(), &compute_device) {
                    Ok(t) => match t.reshape((1, context_len)) {
                        Ok(r) => r,
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

                // Forward step
                let logits = match model.forward(&input_tensor, pos, None) {
                    Ok(l) => l,
                    Err(e) => {
                        let _ = tx.blocking_send(Err(AppError::InferenceError(e.to_string())));
                        break;
                    }
                };

                // Get last token logits tensor: shape [vocab_size]
                let last_logit = match logits.squeeze(0) {
                    Ok(s) => match s.get(context_len - 1) {
                        Ok(l) => l,
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

                // Extract exact dynamic vocab dimension dynamically
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

                Self::apply_repetition_penalty(&mut logits_vec, repetition_penalty, &seen_tokens);

                // Reconstruct tensor using actual dynamic vocab_size on CPU
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

                // Stop conditions
                if Some(next_token_id) == eos_id || Some(next_token_id) == im_end_id {
                    break;
                }

                seen_tokens.insert(next_token_id);

                if let Ok(token_str) = tokenizer_instance.decode(&[next_token_id], true) {
                    if tx.blocking_send(Ok(token_str)).is_err() {
                        break;
                    }
                }

                pos += context_len;
                input_tokens = vec![next_token_id];
                generated_tokens += 1;
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
