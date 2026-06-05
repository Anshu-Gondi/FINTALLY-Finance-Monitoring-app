use std::collections::HashMap;
use crate::core::rag::errors::RagError;
use crate::core::rag::embedding::types::DocumentChunk;
use crate::core::rag::vectorstore::{UsearchStore, MemoryVectorStore};
use super::filters::MetadataFilter;
use super::hybrid::HybridScorer;

pub struct VectorRetriever {
    alpha: f32, // Structural weight bias (e.g., 0.7 dense + 0.3 sparse)
}

impl VectorRetriever {
    pub fn new(alpha: f32) -> Self {
        Self { alpha }
    }

    pub fn retrieve(
        &self,
        query: &str,
        query_vector: &[f32],
        vector_store: &UsearchStore,
        memory_store: &MemoryVectorStore,
        top_k: usize,
        filters: Option<HashMap<String, String>>,
    ) -> Result<Vec<(DocumentChunk, f32)>, RagError> {
        // Query double top_k elements from USearch to maintain top accuracy through metadata filtering
        let graph_matches = vector_store.search_vectors(query_vector, top_k * 2)?;
        let mut final_matches = Vec::new();

        for (chunk_id, dense_score) in graph_matches {
            if let Some(chunk) = memory_store.get_chunk(chunk_id) {
                // Apply optional metadata filters
                if let Some(ref criteria) = filters {
                    if !MetadataFilter::matches(chunk, criteria) {
                        continue;
                    }
                }

                // Compute sparse term coverage metrics
                let sparse_score = HybridScorer::keyword_score(query, &chunk.text);
                let combined_score = HybridScorer::combine_scores(dense_score, sparse_score, self.alpha);

                final_matches.push((chunk.clone(), combined_score));
            }
        }

        // Sort descending by combined hybrid ranking scores
        final_matches.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));
        if final_matches.len() > top_k {
            final_matches.truncate(top_k);
        }

        Ok(final_matches)
    }
}