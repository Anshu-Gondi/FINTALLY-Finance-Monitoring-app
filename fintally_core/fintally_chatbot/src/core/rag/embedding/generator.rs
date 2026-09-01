use std::fs::File;
use std::path::Path;
use std::sync::Arc;
use memmap2::Mmap;

use candle_core::{DType, Device, Tensor};
use candle_nn::VarBuilder;
use candle_transformers::models::bert::{BertModel, Config};
use tokenizers::{Tokenizer, TruncationDirection, TruncationParams, TruncationStrategy};

use crate::core::rag::errors::RagError;
use super::types::ChunkEmbedding;

/// Maximum sequence length supported by standard BGE / BERT position embeddings
const MAX_BERT_SEQ_LEN: usize = 512;

/// Dynamically detect CUDA GPU 0 or fallback to CPU
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
    hidden_size: usize,
}

impl NativeEmbedder {
    /// Initializer targeting your downloaded unquantized BGE Safetensors vault directory
    pub fn load_from_vault<P: AsRef<Path>>(vault_dir: P) -> Result<Self, RagError> {
        let dir = vault_dir.as_ref();
        let config_path = dir.join("config.json");
        let weights_path = dir.join("model.safetensors");
        let tokenizer_path = dir.join("tokenizer.json");

        // 1. Detect hardware target
        let device = select_device()?;

        // 2. Load configuration metadata
        let config_str = std::fs::read_to_string(&config_path)
            .map_err(|e| RagError::EmbeddingError(format!("Failed reading config.json: {e}")))?;
        let config: Config = serde_json::from_str(&config_str)
            .map_err(|e| RagError::EmbeddingError(format!("Malformed embedding configuration structural mapping: {e}")))?;
        let hidden_size = config.hidden_size;

        // 3. Load Tokenizer definition & configure truncation guard
        let mut tokenizer = Tokenizer::from_file(&tokenizer_path)
            .map_err(|e| RagError::EmbeddingError(format!("Failed pulling tokenizer.json engine asset: {e}")))?;

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

        // 5. Select DType based on target hardware
        let dtype = if device.is_cuda() { DType::F16 } else { DType::F32 };

        // 6. Zero-copy VarBuilder directly from the mmap slice
        let vb = VarBuilder::from_slice_safetensors(&mmap[..], dtype, &device)
            .map_err(|e| RagError::EmbeddingError(format!("Failed establishing Candle tensor storage graphs: {e}")))?;

        let model = BertModel::load(vb, &config)
            .map_err(|e| RagError::EmbeddingError(format!("Failed compiling Bert network framework graph parameters: {e}")))?;

        Ok(Self {
            model,
            tokenizer,
            device,
            hidden_size,
        })
    }

    /// Single text vector transformation layer with zero-allocation slicing
    pub fn embed_text(&self, text: &str) -> Result<Vec<f32>, RagError> {
        if text.trim().is_empty() {
            return Err(RagError::EmbeddingError(
                "Inference execution exception: Empty or blank text buffer passed.".to_string(),
            ));
        }

        let tokens = self
            .tokenizer
            .encode(text, true)
            .map_err(|e| RagError::EmbeddingError(format!("Tokenizer indexing failure: {e}")))?;

        let ids_slice = tokens.get_ids();
        let types_slice = tokens.get_type_ids();
        let mask_slice = tokens.get_attention_mask();

        let len = usize::min(ids_slice.len(), MAX_BERT_SEQ_LEN);

        // Build tensors directly from slices without allocating temporary Vecs
        let input_ids = Tensor::from_slice(&ids_slice[..len], (1, len), &self.device)
            .map_err(|e| RagError::EmbeddingError(format!("Failed embedding array token tensor mappings: {e}")))?;

        let token_type_ids = Tensor::from_slice(&types_slice[..len], (1, len), &self.device)
            .map_err(|e| RagError::EmbeddingError(format!("Failed embedding type layer tensor configurations: {e}")))?;

        let attention_mask_tensor = Tensor::from_slice(&mask_slice[..len], (1, len), &self.device)
            .map_err(|e| RagError::EmbeddingError(format!("Failed embedding attention mask tensor configurations: {e}")))?;

        // Forward Pass
        let embeddings = self
            .model
            .forward(&input_ids, &token_type_ids, None)
            .map_err(|e| RagError::EmbeddingError(format!("Candle core neural network execution error: {e}")))?;

        // Pool and L2 Normalize
        self.mean_pooling_and_normalize(&embeddings, &attention_mask_tensor)
    }

    /// Batch text embedding method for bulk RAG ingestion
    pub fn embed_batch(&self, texts: &[&str]) -> Result<Vec<Vec<f32>>, RagError> {
        if texts.is_empty() {
            return Ok(Vec::new());
        }

        let encodings = self
            .tokenizer
            .encode_batch(texts.to_vec(), true)
            .map_err(|e| RagError::EmbeddingError(format!("Batch tokenization failure: {e}")))?;

        let batch_size = encodings.len();
        let max_len = encodings
            .iter()
            .map(|e| usize::min(e.get_ids().len(), MAX_BERT_SEQ_LEN))
            .max()
            .unwrap_or(0);

        if max_len == 0 {
            return Err(RagError::EmbeddingError("Batch contained only empty tokens".to_string()));
        }

        // Prepare flat padded buffers for 2D Tensors [batch_size, max_len]
        let mut flat_ids = Vec::with_capacity(batch_size * max_len);
        let mut flat_types = Vec::with_capacity(batch_size * max_len);
        let mut flat_masks = Vec::with_capacity(batch_size * max_len);

        for enc in &encodings {
            let ids = enc.get_ids();
            let types = enc.get_type_ids();
            let mask = enc.get_attention_mask();
            let len = usize::min(ids.len(), max_len);

            flat_ids.extend_from_slice(&ids[..len]);
            flat_types.extend_from_slice(&types[..len]);
            flat_masks.extend_from_slice(&mask[..len]);

            // Pad sequences up to max_len
            let pad_len = max_len - len;
            if pad_len > 0 {
                flat_ids.resize(flat_ids.len() + pad_len, 0);
                flat_types.resize(flat_types.len() + pad_len, 0);
                flat_masks.resize(flat_masks.len() + pad_len, 0);
            }
        }

        let input_ids = Tensor::from_slice(&flat_ids, (batch_size, max_len), &self.device)?;
        let token_type_ids = Tensor::from_slice(&flat_types, (batch_size, max_len), &self.device)?;
        let attention_mask = Tensor::from_slice(&flat_masks, (batch_size, max_len), &self.device)?;

        let embeddings = self.model.forward(&input_ids, &token_type_ids, None)?;

        // Mean Pool + L2 Normalize across the batch dimension
        self.mean_pooling_and_normalize_batch(&embeddings, &attention_mask, batch_size)
    }

    /// Compute GPU/CPU Mean Pooling + L2 Normalization for a single text input
    fn mean_pooling_and_normalize(&self, embeddings: &Tensor, attention_mask: &Tensor) -> Result<Vec<f32>, RagError> {
        let mask_expanded = attention_mask
            .unsqueeze(2)?
            .broadcast_as(embeddings.shape())?
            .to_dtype(embeddings.dtype())?;

        let sum_embeddings = embeddings.broadcast_mul(&mask_expanded)?.sum(1)?;
        let sum_mask = mask_expanded.sum(1)?;
        let clamped_mask = sum_mask.clamp(1e-9f32, f32::MAX)?;

        let pooled = sum_embeddings.broadcast_div(&clamped_mask)?;

        // Apply L2 Normalization: v / sqrt(max(sum(v^2), 1e-12))
        let norm = pooled.sqr()?.sum_keepdim(1)?.sqrt()?.clamp(1e-12f32, f32::MAX)?;
        let normalized = pooled.broadcast_div(&norm)?;

        let pooled_f32 = normalized.to_dtype(DType::F32)?.squeeze(0)?;
        let finalized_vector = pooled_f32
            .to_vec1::<f32>()
            .map_err(|e| RagError::EmbeddingError(format!("Failed reading vector from tensor: {e}")))?;

        if finalized_vector.len() != self.hidden_size {
            return Err(RagError::EmbeddingError(format!(
                "Model dimension mismatch. Expected {}, got {}",
                self.hidden_size,
                finalized_vector.len()
            )));
        }

        Ok(finalized_vector)
    }

    /// Compute GPU/CPU Mean Pooling + L2 Normalization for a batch of text inputs
    fn mean_pooling_and_normalize_batch(
        &self,
        embeddings: &Tensor,
        attention_mask: &Tensor,
        batch_size: usize,
    ) -> Result<Vec<Vec<f32>>, RagError> {
        let mask_expanded = attention_mask
            .unsqueeze(2)?
            .broadcast_as(embeddings.shape())?
            .to_dtype(embeddings.dtype())?;

        let sum_embeddings = embeddings.broadcast_mul(&mask_expanded)?.sum(1)?;
        let sum_mask = mask_expanded.sum(1)?;
        let clamped_mask = sum_mask.clamp(1e-9f32, f32::MAX)?;

        let pooled = sum_embeddings.broadcast_div(&clamped_mask)?;

        // L2 Normalize
        let norm = pooled.sqr()?.sum_keepdim(1)?.sqrt()?.clamp(1e-12f32, f32::MAX)?;
        let normalized = pooled.broadcast_div(&norm)?.to_dtype(DType::F32)?;

        let flat_vectors = normalized
            .to_vec2::<f32>()
            .map_err(|e| RagError::EmbeddingError(format!("Failed reading batch matrix: {e}")))?;

        Ok(flat_vectors)
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

    #[inline]
    pub fn generate(&self, chunk_id: u64, text: &str) -> Result<ChunkEmbedding, RagError> {
        let vector = self.embedder.embed_text(text)?;
        Ok(ChunkEmbedding::new(chunk_id, vector))
    }

    /// Batch generator for parallel chunk processing
    pub fn generate_batch(&self, chunks: &[(u64, &str)]) -> Result<Vec<ChunkEmbedding>, RagError> {
        let texts: Vec<&str> = chunks.iter().map(|(_, text)| *text).collect();
        let vectors = self.embedder.embed_batch(&texts)?;

        Ok(chunks
            .iter()
            .zip(vectors.into_iter())
            .map(|((id, _), vec)| ChunkEmbedding::new(*id, vec))
            .collect())
    }
}
