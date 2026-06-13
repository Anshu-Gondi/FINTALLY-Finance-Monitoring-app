use usearch::{Index, IndexOptions, MetricKind, ScalarKind};
use crate::core::rag::errors::RagError;

pub struct UsearchStore {
    index: Index,
    dimensions: usize,
}

impl UsearchStore {
    /// Allocates a new native HNSW vector cluster space with configurable initial capacity.
    pub fn new(dimensions: usize, initial_capacity: usize) -> Result<Self, RagError> {
        let mut options = IndexOptions::default();
        options.dimensions = dimensions;
        options.metric = MetricKind::Cos;            // Fast directional cosine distance metrics
        options.quantization = ScalarKind::I8;       // Native 8-bit vector array scaling optimization
        
        let index = Index::new(&options)
            .map_err(|_| RagError::VectorStoreError("Failed to initialize native USearch graph index context.".into()))?;

        // Pre-allocating the correct volume up-front prevents mid-ingestion 
        // thread freezes caused by underlying C++ graph resizing blocks.
        if initial_capacity > 0 {
            index.reserve(initial_capacity)
                .map_err(|_| RagError::VectorStoreError("Failed to reserve storage buffer steps within native index allocator.".into()))?;
        }

        Ok(Self { index, dimensions })
    }

    /// Indexes an f32 vector into the quantized USearch matrix under a given chunk ID.
    /// Safely auto-expands internal node allocations if capacity ceilings are breached.
    pub fn add_vector(&self, chunk_id: u64, vector: &[f32]) -> Result<(), RagError> {
        if vector.len() != self.dimensions {
            return Err(RagError::VectorStoreError(
                format!("Vector dimension layout size mismatch: expected {}, got {}.", self.dimensions, vector.len())
            ));
        }

        // --- DYNAMIC EXPANSION GUARD ---
        // If the index length matches or exceeds its current structural capacity boundary
        // (common after running load_index() on a static file), extend capacity buffer slots.
        let current_size = self.index.size();
        let current_capacity = self.index.capacity();

        if current_size >= current_capacity {
            // Dynamically scale out slots. Adding a safety headroom padding of 500 items 
            // stops performance degradation from repeated incremental reallocations.
            let expanded_target = current_size + 500;
            self.index.reserve(expanded_target)
                .map_err(|_| RagError::VectorStoreError(
                    format!("Failed to auto-expand HNSW vector storage limits up to target slot count: {}.", expanded_target)
                ))?;
        }
        // -------------------------------

        self.index.add(chunk_id, vector)
            .map_err(|_| RagError::VectorStoreError(format!("Failed to register vector item ID: {}", chunk_id)))?;

        Ok(())
    }

    /// Finds matching vectors inside the HNSW index layer using zero-branch results processing
    pub fn search_vectors(&self, query_vector: &[f32], top_k: usize) -> Result<Vec<(u64, f32)>, RagError> {
        if query_vector.len() != self.dimensions {
            return Err(RagError::VectorStoreError("Query vector dimension target configuration out of sync.".into()));
        }

        let matches = self.index.search(query_vector, top_k)
            .map_err(|_| RagError::VectorStoreError("Native graph query search routine step execution failure.".into()))?;

        // Pre-allocate the result array to prevent incremental reallocation spikes
        let mut results = Vec::with_capacity(matches.keys.len());

        // CELERON TUNING: Iterator Fusing
        results.extend(
            matches.keys.iter()
                .zip(matches.distances.iter())
                .map(|(&key, &distance)| {
                    // USearch returns raw distance. Convert to a similarity score where higher is better
                    let similarity_score = 1.0 - distance;
                    (key, similarity_score)
                })
        );

        Ok(results)
    }

    /// Flushes graph layouts straight to static disk files safely
    pub fn save_index(&self, path: &str) -> Result<(), RagError> {
        self.index.save(path)
            .map_err(|_| RagError::VectorStoreError("Failed to dump native graph data array sets onto disk path file target.".into()))?;
        Ok(())
    }

    /// Reloads compiled index mappings from local file records
    pub fn load_index(&self, path: &str) -> Result<(), RagError> {
        self.index.load(path)
            .map_err(|_| RagError::VectorStoreError("Failed to fetch binary data arrays from local filesystem targets.".into()))?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_store_initialization_and_capacity() {
        let store_gen = UsearchStore::new(128, 100);
        assert!(store_gen.is_ok(), "Failed to allocate native USearch memory blocks");
        
        let store = store_gen.unwrap();
        assert_eq!(store.dimensions, 128);

        let zero_capacity_store = UsearchStore::new(64, 0);
        assert!(zero_capacity_store.is_ok());
    }

    #[test]
    fn test_add_vector_dimension_guards() {
        let store = UsearchStore::new(4, 10).unwrap();

        let matching_vector = vec![0.25, 0.5, 0.75, 1.0];
        assert!(store.add_vector(42, &matching_vector).is_ok());

        let broken_vector = vec![0.1, 0.2, 0.3];
        let bad_append = store.add_vector(43, &broken_vector);
        
        assert!(bad_append.is_err());
        match bad_append.unwrap_err() {
            RagError::VectorStoreError(msg) => {
                assert!(msg.contains("Vector dimension layout size mismatch"));
            },
            other => panic!("Expected VectorStoreError from matrix size divergence, caught: {:?}", other),
        }
    }

    #[test]
    fn test_auto_expansion_over_initial_capacity() {
        // Create an index limited intentionally to exactly 1 slot
        let store = UsearchStore::new(2, 1).unwrap();

        // Fill the initial slot
        assert!(store.add_vector(101, &vec![1.0, 0.0]).is_ok());

        // This second vector should trigger the auto-expansion guard instead of crashing
        assert!(store.add_vector(102, &vec![0.0, 1.0]).is_ok(), "Index failed to auto-expand capacity bounds!");
        assert!(store.index.capacity() >= 2);
    }

    #[test]
    fn test_search_fused_iterator_and_similarity_conversion() {
        let store = UsearchStore::new(3, 5).unwrap();
        
        let item_a = vec![1.0, 0.0, 0.0];
        let item_b = vec![0.0, 1.0, 0.0];
        
        store.add_vector(1001, &item_a).unwrap();
        store.add_vector(1002, &item_b).unwrap();

        let query_point = vec![1.0, 0.0, 0.0];
        let query_execution = store.search_vectors(&query_point, 2);
        
        assert!(query_execution.is_ok());
        let records = query_execution.unwrap();

        assert!(!records.is_empty(), "Search pipeline returned cold/empty arrays");
        
        let (first_matched_id, similarity_score) = records[0];
        assert_eq!(first_matched_id, 1001);
        
        assert!(similarity_score > 0.99, "Similarity calculation failed inversion mapping transformations");
    }

    #[test]
    fn test_search_vector_dimension_mismatch_guard() {
        let store = UsearchStore::new(384, 5).unwrap();
        let bad_query_point = vec![0.5, 0.5];
        
        let result = store.search_vectors(&bad_query_point, 5);
        assert!(result.is_err());
        
        match result.unwrap_err() {
            RagError::VectorStoreError(msg) => {
                assert!(msg.contains("Query vector dimension target configuration out of sync"));
            },
            other => panic!("Expected dimension out of sync exception, caught: {:?}", other),
        }
    }
}