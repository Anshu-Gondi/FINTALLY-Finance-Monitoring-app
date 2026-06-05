use std::collections::HashMap;
use crate::core::rag::embedding::types::DocumentChunk;
use crate::core::rag::errors::RagError;
use super::similarity::Similarity;

pub struct MemoryVectorStore {
    chunks: HashMap<u64, DocumentChunk>,
    vectors: HashMap<u64, Vec<f32>>,
}

impl MemoryVectorStore {
    pub fn new() -> Self {
        Self {
            chunks: HashMap::new(),
            vectors: HashMap::new(),
        }
    }

    pub fn insert(&mut self, chunk: DocumentChunk, vector: Vec<f32>) {
        let id = chunk.id;
        self.chunks.insert(id, chunk);
        self.vectors.insert(id, vector);
    }

    pub fn get_chunk(&self, id: u64) -> Option<&DocumentChunk> {
        self.chunks.get(&id)
    }

    /// Performs a linear brute-force scan across records (useful for verifying data integrity)
    pub fn search(&self, query_vector: &[f32], top_k: usize) -> Result<Vec<(DocumentChunk, f32)>, RagError> {
        let mut matches = Vec::new();

        for (id, vec) in &self.vectors {
            if vec.len() == query_vector.len() {
                let score = Similarity::cosine_similarity(query_vector, vec);
                if let Some(chunk) = self.chunks.get(id) {
                    matches.push((chunk.clone(), score));
                }
            }
        }

        // Sort by highest similarity match descending
        matches.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));
        if matches.len() > top_k {
            matches.truncate(top_k);
        }

        Ok(matches)
    }

    pub fn clear(&mut self) {
        self.chunks.clear();
        self.vectors.clear();
    }
}