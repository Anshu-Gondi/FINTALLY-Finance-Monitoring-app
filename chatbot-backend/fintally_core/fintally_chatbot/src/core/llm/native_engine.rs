use crate::core::llm::engine::{LlmEngine, CancelableStream};
use crate::core::utils::errors::AppError;

use async_trait::async_trait;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::{mpsc, Mutex};
use tokio_stream::wrappers::ReceiverStream;
use tokio_util::sync::CancellationToken;

// Candle ML ecosystem matching workspace v0.11.0
use candle_core::{Device, Tensor, DType};
use candle_transformers::models::qwen2::{Config, Model};
use tokenizers::Tokenizer;

/// High-Performance Local Candle Inference Engine running unquantized Qwen.
pub struct NativeLlamaEngine {
    pub model: Arc<Mutex<Model>>,
    pub tokenizer: Tokenizer,
    pub device: Device,
    /// Hard resource lock guaranteeing sequential execution on limited CPUs (Issue 2)
    pub global_inference_lock: Arc<Mutex<()>>,
}

impl NativeLlamaEngine {
    /// Loads unquantized Qwen weights via workspace memmap2 zero-copy mapping
    pub fn load_from_vault(vault_dir: &str) -> Result<Self, String> {
        let path = PathBuf::from(vault_dir);
        let config_path = path.join("config.json");
        let tokenizer_path = path.join("tokenizer.json");
        let weights_path = path.join("model.safetensors");

        let device = Device::Cpu;
        
        // 1. Parse configuration parameters
        let config_str = std::fs::read_to_string(&config_path)
            .map_err(|e| format!("Failed to read config.json: {e}"))?;
        let config: Config = serde_json::from_str(&config_str)
            .map_err(|e| format!("Malformed config payload: {e}"))?;

        // 2. Initialize Tokenizer structure matching v0.22.0
        let tokenizer = Tokenizer::from_file(&tokenizer_path)
            .map_err(|e| format!("Failed to parse tokenizer.json: {e}"))?;

        // 3. Initialize VarBuilder directly from the safetensors file paths
        // Candle's native constructor handles opening, mapping, and reading the file safely.
        let vb = unsafe {
            candle_nn::VarBuilder::from_mmaped_safetensors(
                &[weights_path], 
                DType::F32, 
                &device
            ).map_err(|e| format!("Failed to initialize memory-mapped VarBuilder: {e}"))?
        };

        // 4. Construct the native Qwen graph model topology using the config and builder
        let model = Model::new(&config, vb)
            .map_err(|e| format!("Failed constructing native Qwen graph topologies: {e}"))?;

        Ok(Self {
            model: Arc::new(Mutex::new(model)),
            tokenizer,
            device,
            global_inference_lock: Arc::new(Mutex::new(())),
        })
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

        // Encode prompt tokens natively
        let tokens = self.tokenizer.encode(prompt, true)
            .map_err(|e| AppError::InferenceError(format!("Token encoding error: {e}")))?;
        let raw_tokens = tokens.get_ids().to_vec();

        let model_lock = self.model.clone();
        let tokenizer_instance = self.tokenizer.clone();
        let compute_device = self.device.clone();
        let cpu_execution_barrier = self.global_inference_lock.clone();
        let cancel_child = cancel.clone();

        tokio::spawn(async move {
            if cancel_child.is_cancelled() { return; }

            // Acquire exclusive CPU execution privileges across threads (Issue 2)
            let _cpu_guard = cpu_execution_barrier.lock().await;
            let mut model = model_lock.lock().await;

            let mut input_tokens = raw_tokens;
            let mut generated_tokens = 0;

            // Simple sample generation loop handling end-of-sequence parameters
            while generated_tokens < max_tokens && !cancel_child.is_cancelled() {
                let context_len = input_tokens.len();
                let input_tensor = match Tensor::new(input_tokens.as_slice(), &compute_device) {
                    Ok(t) => match t.reshape((1, context_len)) {
                        Ok(r) => r,
                        Err(e) => { let _ = tx.send(Err(AppError::InferenceError(e.to_string()))).await; break; }
                    },
                    Err(e) => { let _ = tx.send(Err(AppError::InferenceError(e.to_string()))).await; break; }
                };

                // Forward pass evaluation through Candle v0.11.0 structures
                // FIXED: Supplied missing 3rd argument (None) representing structural mask tokens
                let logits = match model.forward(&input_tensor, 0, None) {
                    Ok(l) => l,
                    Err(e) => { let _ = tx.send(Err(AppError::InferenceError(e.to_string()))).await; break; }
                };

                let logits = match logits.squeeze(0) {
                    Ok(l) => match l.get(logits.dims()[1] - 1) {
                        Ok(last_logit) => last_logit,
                        Err(e) => { let _ = tx.send(Err(AppError::InferenceError(e.to_string()))).await; break; }
                    },
                    Err(e) => { let _ = tx.send(Err(AppError::InferenceError(e.to_string()))).await; break; }
                };

                let next_token_id = match logits.argmax(0) {
                    Ok(t) => match t.to_scalar::<u32>() {
                        Ok(val) => val,
                        Err(e) => { let _ = tx.send(Err(AppError::InferenceError(e.to_string()))).await; break; }
                    },
                    Err(e) => { let _ = tx.send(Err(AppError::InferenceError(e.to_string()))).await; break; }
                };

                // Check for standard EOS boundaries matching vocabulary specifications
                if Some(next_token_id) == tokenizer_instance.get_vocab(true).get("<|endoftext|>").copied() {
                    break;
                }

                if let Ok(token_str) = tokenizer_instance.decode(&[next_token_id], true) {
                    if tx.send(Ok(token_str)).await.is_err() {
                        break; // Channel closed
                    }
                }

                input_tokens.push(next_token_id);
                generated_tokens += 1;

                tokio::task::yield_now().await;
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