use std::path::Path;
use std::fs::File;
use std::sync::Arc;
use memmap2::Mmap;

use candle_core::{Device, Tensor, DType};
use candle_nn::VarBuilder;
use candle_transformers::models::bert::{BertModel, Config};
use tokenizers::{Tokenizer, TruncationParams, TruncationStrategy, TruncationDirection};

use crate::core::rag::errors::RagError;
use super::types::ChunkEmbedding;

/// Maximum sequence length supported by standard BGE / BERT position embeddings
const MAX_BERT_SEQ_LEN: usize = 512;

/// Helper function to dynamically detect CUDA GPU 0 or fallback to CPU
fn select_device() -> Result<Device, RagError> {
    if candle_core::utils::cuda_is_available() {
        println!("🚀 CUDA GPU detected! Initializing RAG Embedder on GPU 0...");
        Device::new_cuda(0)
            .map_err(|e| RagError::EmbeddingError(format!("Failed to initialize CUDA device 0: {e}")))
    } else {
        println!("⚠️ CUDA not available/detected. Running RAG Embedder on CPU.");
        Ok(Device::Cpu)
    }
}

/// Direct, native transformer loader utilizing Candle with dynamic CUDA / CPU support.
pub struct NativeEmbedder {
    model: BertModel,
    tokenizer: Tokenizer,
    device: Device,
}

impl NativeEmbedder {
    /// Initializer targeting your downloaded unquantized BGE Safetensors vault directory
    pub fn load_from_vault<P: AsRef<Path>>(vault_dir: P) -> Result<Self, RagError> {
        let dir = vault_dir.as_ref();
        let config_path = dir.join("config.json");
        let weights_path = dir.join("model.safetensors");
        let tokenizer_path = dir.join("tokenizer.json");

        // 1. Detect and allocate hardware execution target dynamically
        let device = select_device()?;

        // 2. Load configuration metadata
        let config_str = std::fs::read_to_string(&config_path)
            .map_err(|e| RagError::EmbeddingError(format!("Failed reading config.json: {e}")))?;
        let config: Config = serde_json::from_str(&config_str)
            .map_err(|e| RagError::EmbeddingError(format!("Malformed embedding configuration structural mapping: {e}")))?;

        // 3. Load Tokenizer definition & configure truncation guard
        let mut tokenizer = Tokenizer::from_file(&tokenizer_path)
            .map_err(|e| RagError::EmbeddingError(format!("Failed pulling tokenizer.json engine asset: {e}")))?;

        // Enforce 512 token truncation to prevent CUDA index_select position embedding crashes
        let truncation_params = TruncationParams {
            max_length: MAX_BERT_SEQ_LEN,
            strategy: TruncationStrategy::LongestFirst,
            stride: 0,
            direction: TruncationDirection::Right,
        };
        let _ = tokenizer.with_truncation(Some(truncation_params));

        // 4. Memory Map the Safetensors Array
        let file = File::open(&weights_path)
            .map_err(|e| RagError::EmbeddingError(format!("Weights not found at path {}: {e}", weights_path.display())))?;
        let mmap = unsafe { Mmap::map(&file) }
            .map_err(|e| RagError::EmbeddingError(format!("Failed mapping model layers into virtual memory address space: {e}")))?;

        // 5. Select DType based on target hardware (F16 for GPU performance, F32 for CPU accuracy)
        let dtype = if device.is_cuda() { DType::F16 } else { DType::F32 };

        // 6. Construct internal weights layout via Candle
        let vb = VarBuilder::from_buffered_safetensors(mmap.to_vec(), dtype, &device)
            .map_err(|e| RagError::EmbeddingError(format!("Failed establishing Candle tensor storage graphs: {e}")))?;

        let model = BertModel::load(vb, &config)
            .map_err(|e| RagError::EmbeddingError(format!("Failed compiling Bert network framework graph parameters: {e}")))?;

        Ok(Self { model, tokenizer, device })
    }

    /// Direct text vector transformation layer
    pub fn embed_text(&self, text: &str) -> Result<Vec<f32>, RagError> {
        if text.trim().is_empty() {
            return Err(RagError::EmbeddingError("Inference execution exception: Empty or blank text buffer passed.".to_string()));
        }

        // Tokenize text input slice
        let tokens = self.tokenizer.encode(text, true)
            .map_err(|e| RagError::EmbeddingError(format!("Tokenizer indexing failure: {e}")))?;

        let mut token_ids = tokens.get_ids().to_vec();
        let mut token_type_ids = tokens.get_type_ids().to_vec();

        // FAIL-SAFE BOUND GUARD: Hard truncate slice if tokenizer truncation settings failed
        if token_ids.len() > MAX_BERT_SEQ_LEN {
            token_ids.truncate(MAX_BERT_SEQ_LEN);
            token_type_ids.truncate(MAX_BERT_SEQ_LEN);
        }

        // Structure Candle tensors targeting the selected device (CUDA/CPU)
        let input_ids = Tensor::new(token_ids.as_slice(), &self.device)
            .map_err(|e| RagError::EmbeddingError(format!("Failed embedding array token tensor mappings: {e}")))?
            .unsqueeze(0)?; // Pack into dimension shapes: [1, seq_len]

        let token_type_ids = Tensor::new(token_type_ids.as_slice(), &self.device)
            .map_err(|e| RagError::EmbeddingError(format!("Failed embedding type layer tensor configurations: {e}")))?
            .unsqueeze(0)?;

        // Run direct Forward Matrix Vector Pass on GPU or CPU
        let embeddings = self.model.forward(&input_ids, &token_type_ids, None)
            .map_err(|e| RagError::EmbeddingError(format!("Candle core neural network execution error: {e}")))?;

        // Apply Mean Pooling to build output topology
        let pooled_vector = self.mean_pooling(&embeddings, &tokens)?;

        Ok(pooled_vector)
    }

    /// Standard Mean Pooling routine tailored for Sentence Transformers
    fn mean_pooling(&self, embeddings: &Tensor, tokens: &tokenizers::Encoding) -> Result<Vec<f32>, RagError> {
        let attention_mask = tokens.get_attention_mask();

        // Dimensions: [1, seq_len, hidden_size]
        let (_n_batch, seq_len, hidden_size) = embeddings.dims3()
            .map_err(|e| RagError::EmbeddingError(format!("Invalid matrix dimensional configuration: {e}")))?;

        // Ensure tensor is cast to F32 when converting to vector array regardless of compute DType
        let embeddings_f32 = embeddings.to_dtype(DType::F32)
            .map_err(|e| RagError::EmbeddingError(format!("Failed casting embeddings to float32: {e}")))?;

        let embeddings_data = embeddings_f32.to_vec3::<f32>()
            .map_err(|e| RagError::EmbeddingError(format!("Failed reading raw scalar matrices from calculation graphs: {e}")))?;

        let mut sum_embeddings = vec![0.0f32; hidden_size];
        let mut sum_mask = 0.0f32;

        // Loop over sequence elements bounded by the actual embedding output matrix size
        for i in 0..seq_len {
            let mask_val = attention_mask.get(i).cloned().unwrap_or(1) as f32;
            sum_mask += mask_val;

            for j in 0..hidden_size {
                sum_embeddings[j] += embeddings_data[0][i][j] * mask_val;
            }
        }

        // Prevent division-by-zero boundaries for safety strings
        let divisor = if sum_mask > 0.0 { sum_mask } else { 1.0 };
        let finalized_vector: Vec<f32> = sum_embeddings.into_iter().map(|v| v / divisor).collect();

        if finalized_vector.len() != 384 {
            return Err(RagError::EmbeddingError(format!(
                "Model dimension topology mismatch. Expected 384, generated {}", finalized_vector.len()
            )));
        }

        Ok(finalized_vector)
    }
}

// ==============================================================================
// TOP-LEVEL RAG INGESTION PIPELINE ENTRY POINT
// ==============================================================================
pub struct EmbeddingGenerator {
    embedder: Arc<NativeEmbedder>,
}

impl EmbeddingGenerator {
    #[inline]
    pub fn new(embedder: Arc<NativeEmbedder>) -> Self {
        Self { embedder }
    }

    /// Generates embeddings and packages them directly into an exact-sized chunk wrapper
    #[inline]
    pub fn generate(&self, chunk_id: u64, text: &str) -> Result<ChunkEmbedding, RagError> {
        let vector = self.embedder.embed_text(text)?;
        Ok(ChunkEmbedding::new(chunk_id, vector))
    }
}

// ==============================================================================
// 100% PURE NATIVE RUST UNIT TESTING SUITE
// ==============================================================================
#[cfg(test)]
mod tests {
    use super::*;

    const BGE_VAULT_PATH: &str = "../llm_models/embedding/bge_safetensors_output";

    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_native_embedder_successful_forward_pass_roundtrip() {
        if !Path::new(BGE_VAULT_PATH).exists() {
            println!("skipping test: Real bge vault path not found locally.");
            return;
        }

        let embedder = NativeEmbedder::load_from_vault(BGE_VAULT_PATH).unwrap();
        let generator = EmbeddingGenerator::new(Arc::new(embedder));

        let text_sample = "FinTally high-performance local financial analytics telemetry transaction matrix vector loop.";
        let chunk_id = 777u64;

        let result = generator.generate(chunk_id, text_sample);
        assert!(result.is_ok(), "Failed to run native Candle inference: {:?}", result.err());

        let chunk_embedding = result.unwrap();

        assert_eq!(chunk_embedding.chunk_id, chunk_id);
        assert_eq!(chunk_embedding.dimension_size(), 384);
        assert!(chunk_embedding.vector[0] != 0.0);
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_native_embedder_bubbles_empty_text_errors() {
        if !Path::new(BGE_VAULT_PATH).exists() { return; }

        let embedder = NativeEmbedder::load_from_vault(BGE_VAULT_PATH).unwrap();
        let generator = EmbeddingGenerator::new(Arc::new(embedder));

        let error_result = generator.generate(101, "   ");
        assert!(error_result.is_err(), "System accepted whitespace streams without erroring out.");

        match error_result.unwrap_err() {
            RagError::EmbeddingError(msg) => {
                assert!(msg.contains("Inference execution exception"), "Intercepted misaligned error layout: {}", msg);
            }
            _ => panic!("Expected localized RagError::EmbeddingError wrapper variant structure."),
        }
    }
}
