use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Represents a raw text split from a document, before vector conversion
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DocumentChunk {
    pub id: u64,               // Globally unique 64-bit ID required for fast USearch indexing
    pub document_id: String,   // Track back to the source file
    pub text: String,          // The actual textual facts/content
    pub sequence_index: usize, // Tracks structural order within the original file
    pub metadata: HashMap<String, String>,
}

/// Dense floating-point vector wrapper.
/// Keeps vector allocation tightly aligned in continuous f32 blocks 
/// to allow your Celeron CPU to take advantage of sequential memory cache hits.
#[derive(Debug, Clone)]
pub struct ChunkEmbedding {
    pub chunk_id: u64,
    pub vector: Vec<f32>,
}

impl ChunkEmbedding {
    pub fn new(chunk_id: u64, vector: Vec<f32>) -> Self {
        Self { chunk_id, vector }
    }

    /// Helper to verify vector health and check dimension alignment
    pub fn dimension_size(&self) -> usize {
        self.vector.len()
    }
}