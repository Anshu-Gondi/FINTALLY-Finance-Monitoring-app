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
        
        // 1. CELERON TUNING: Pre-allocate memory capacity for the exact candidate size
        // Storing lightweight reference pointers (&DocumentChunk) ensures ZERO heap allocations in the loop.
        let mut intermediate_matches: Vec<(&DocumentChunk, f32)> = Vec::with_capacity(graph_matches.len());

        for (chunk_id, dense_score) in graph_matches {
            if let Some(chunk) = memory_store.get_chunk(chunk_id) {
                // Apply optional metadata filters
                if let Some(ref criteria) = filters {
                    if !MetadataFilter::matches(chunk, criteria) {
                        continue;
                    }
                }

                // Compute sparse term coverage metrics using optimized zero-alloc methods
                let sparse_score = HybridScorer::keyword_score(query, &chunk.text);
                let combined_score = HybridScorer::combine_scores(dense_score, sparse_score, self.alpha);

                // Zero-copy: just store the pointer reference and the score
                intermediate_matches.push((chunk, combined_score));
            }
        }

        // 2. USE UNSTABLE SORT FOR IN-PLACE PERFORMANCE
        // Standard sort allocates an auxiliary backup array on the heap. 
        // sort_unstable_by sorts in-place using O(1) auxiliary memory space—critical for 8 GB RAM limits.
        intermediate_matches.sort_unstable_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));
        
        if intermediate_matches.len() > top_k {
            intermediate_matches.truncate(top_k);
        }

        // 3. POST-TRUNCATION CLONING
        // Now that the pool has shrunk down to the actual top_k winners, we safely execute 
        // clones ONLY on the documents that Python will actually use.
        let final_matches = intermediate_matches
            .into_iter()
            .map(|(chunk, score)| (chunk.clone(), score))
            .collect();

        Ok(final_matches)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::rag::vectorstore::{UsearchStore, MemoryVectorStore};
    use crate::core::rag::embedding::types::DocumentChunk;
    

    /// Inline helper to generate standard document chunks matching system schemas
    fn make_mock_chunk(id: u64, text: &str) -> DocumentChunk {
        DocumentChunk {
            id,
            document_id: format!("doc_{}", id).into(), // New mandatory field
            sequence_index: 0,                         // New mandatory field
            text: text.into(),                         // Converts String/&str to Box<str>
            metadata: Box::from([]),                   // Assuming Boxed slice of pairs
        }
    }

    // ==========================================
    // 1. PIPELINE INTEGRATION & TRUNCATION
    // ==========================================

    #[test]
    fn test_hybrid_retrieval_truncates_to_exact_top_k() {
        let retriever = VectorRetriever::new(0.5);
        let vector_store = UsearchStore::new(3, 10).expect("Failed to init USearch");
        let mut memory_store = MemoryVectorStore::new();

        // Sync items
        let chunks = vec![
            make_mock_chunk(1, "Tax allocation ledger records."),
            make_mock_chunk(2, "Corporate checking account deposits."),
            make_mock_chunk(3, "Quarterly balance reconciliation statements."),
        ];

        for (i, chunk) in chunks.into_iter().enumerate() {
            let vec = match i {
                0 => vec![1.0, 0.0, 0.0],
                1 => vec![0.0, 1.0, 0.0],
                _ => vec![0.0, 0.0, 1.0],
            };
            memory_store.insert(chunk, vec.clone());
            vector_store.add_vector(i as u64 + 1, &vec).unwrap();
        }

        let query_vector = vec![1.0, 0.0, 0.0];
        let retrieval_res = retriever.retrieve(
            "ledger",
            &query_vector,
            &vector_store,
            &memory_store,
            2,
            None,
        );

        assert!(retrieval_res.is_ok());
        assert_eq!(retrieval_res.unwrap().len(), 2, "Retriever failed to truncate candidate pools.");
    }

    // ==========================================
    // 2. DESYNC AND BOUNDARY ROBUSTNESS
    // ==========================================

    #[test]
    fn test_missing_memory_chunks_are_skipped_gracefully() {
        let retriever = VectorRetriever::new(0.7);
        let vector_store = UsearchStore::new(3, 5).unwrap();
        let memory_store = MemoryVectorStore::new(); // Purposely left cold/empty

        // Index an active key into USearch graph matrix map
        vector_store.add_vector(888, &[0.5, 0.5, 0.5]).unwrap();

        let query_vector = vec![0.5, 0.5, 0.5];
        let search_execution = retriever.retrieve(
            "anomaly test",
            &query_vector,
            &vector_store,
            &memory_store,
            5,
            None,
        );

        // System must bypass the mismatched/missing cache index without panicking or returning errors
        assert!(search_execution.is_ok());
        assert!(
            search_execution.unwrap().is_empty(),
            "Retriever failed to bypass missing document mapping reference items."
        );
    }

    #[test]
    fn test_empty_vector_store_returns_empty_results() {
        let retriever = VectorRetriever::new(0.3);
        let vector_store = UsearchStore::new(384, 10).unwrap();
        let memory_store = MemoryVectorStore::new();

        let blank_query_vector = vec![0.0; 384];
        let results = retriever.retrieve(
            "empty index target search",
            &blank_query_vector,
            &vector_store,
            &memory_store,
            5,
            None,
        ).unwrap();

        assert!(results.is_empty(), "Retriever populated phantom elements from cold index layouts.");
    }
}