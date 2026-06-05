use crate::core::rag::errors::RagError;
use super::python_embedder::PythonEmbedder;
use super::types::ChunkEmbedding;

pub struct EmbeddingGenerator {
    embedder: PythonEmbedder,
}

impl EmbeddingGenerator {
    pub fn new() -> Self {
        Self {
            embedder: PythonEmbedder::new(),
        }
    }

    pub fn generate(&self, chunk_id: u64, text: &str) -> Result<ChunkEmbedding, RagError> {
        let vector = self.embedder.embed_text(text)?;
        Ok(ChunkEmbedding::new(chunk_id, vector))
    }
}