use std::collections::HashMap;
use crate::core::rag::embedding::types::DocumentChunk;
use crate::core::rag::errors::RagError;
use super::similarity::Similarity;

pub struct MemoryVectorStore {
    // Retained for O(1) direct chunk retrieval by ID
    chunks: HashMap<u64, DocumentChunk>,
    // CELERON TUNING: Flat array storage replaces the second HashMap.
    // This allows sequential brute-force loops to leverage CPU cache prefetching.
    flat_vectors: Vec<(u64, Box<[f32]>)>,
}

impl MemoryVectorStore {
    pub fn new() -> Self {
        Self {
            chunks: HashMap::new(),
            flat_vectors: Vec::new(),
        }
    }

    pub fn insert(&mut self, chunk: DocumentChunk, vector: Vec<f32>) {
        let id = chunk.id;
        self.chunks.insert(id, chunk);
        // Box the slice to drop the redundant 8-byte capacity field from Vec<f32>
        self.flat_vectors.push((id, vector.into_boxed_slice()));
    }

    #[inline]
    pub fn get_chunk(&self, id: u64) -> Option<&DocumentChunk> {
        self.chunks.get(&id)
    }

    /// Performs a linear brute-force scan across records with optimal cache alignment
    pub fn search(
        &self,
        query_vector: &[f32],
        top_k: usize
    ) -> Result<Vec<(DocumentChunk, f32)>, RagError> {
        if self.flat_vectors.is_empty() {
            return Ok(Vec::new());
        }

        // 1. Pre-allocate reference arrays on the stack to prevent dynamic loop scaling
        let mut intermediate_matches: Vec<(&DocumentChunk, f32)> = Vec::with_capacity(
            self.flat_vectors.len()
        );

        // 2. Continuous linear stream pass over adjacent memory addresses
        for (id, vec) in &self.flat_vectors {
            if vec.len() == query_vector.len() {
                let score = Similarity::cosine_similarity(query_vector, vec);

                // Fetch the chunk reference from the map using zero-copy matching
                if let Some(chunk) = self.chunks.get(id) {
                    intermediate_matches.push((chunk, score));
                }
            }
        }

        // 3. In-place unstable sort without auxiliary memory heap usage
        intermediate_matches.sort_unstable_by(|a, b|
            b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal)
        );

        if intermediate_matches.len() > top_k {
            intermediate_matches.truncate(top_k);
        }

        // 4. POST-TRUNCATION CLONING
        // Only pay the allocation penalty for the final top_k documents
        let final_matches = intermediate_matches
            .into_iter()
            .map(|(chunk, score)| (chunk.clone(), score))
            .collect();

        Ok(final_matches)
    }

    pub fn clear(&mut self) {
        self.chunks.clear();
        self.flat_vectors.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::rag::embedding::types::DocumentChunk;
    

    /// Helper to easily spawn dummy document chunks matching your schema fields
    fn make_mock_chunk(id: u64, text: &str) -> DocumentChunk {
        DocumentChunk {
            id,
            document_id: "default_doc".into(), // Add this field
            sequence_index: 0, // Add this field
            text: text.into(), // Use .into() to convert String/&str to Box<str>
            metadata: Box::from([]), // Assuming your metadata is now a box-slice
        }
    }

    #[test]
    fn test_empty_store_returns_empty_search() {
        let store = MemoryVectorStore::new();
        let query = vec![0.1, 0.2, 0.3];

        let result = store.search(&query, 5);
        assert!(result.is_ok());
        assert!(result.unwrap().is_empty(), "Empty store should return zero matches safely.");
    }

    #[test]
    fn test_insertion_and_direct_chunk_lookup() {
        let mut store = MemoryVectorStore::new();
        let chunk = make_mock_chunk(42, "Direct memory allocation lookup verification.");
        let vector = vec![1.0, 0.0, 0.0];

        store.insert(chunk.clone(), vector);

        // Verify O(1) hash map retrieval path
        let fetched = store.get_chunk(42);
        assert!(fetched.is_some());
        assert_eq!(
            fetched.unwrap().text,
            "Direct memory allocation lookup verification.".to_string().into_boxed_str()
        );
        // Verify missing key requests return None safely
        assert!(store.get_chunk(999).is_none());
    }

    #[test]
    fn test_search_ranking_sort_order_and_truncation() {
        let mut store = MemoryVectorStore::new();

        // Target search query point
        let query_vector = vec![1.0, 0.0, 0.0];

        // 1. Chunk A: Exactly matches the query profile (Expected top score ~1.0)
        let chunk_a = make_mock_chunk(101, "Perfect directional match vector.");
        store.insert(chunk_a, vec![1.0, 0.0, 0.0]);

        // 2. Chunk B: Perpendicular/Orthogonal profile (Expected middle score ~0.0)
        let chunk_b = make_mock_chunk(102, "Orthogonal vector offset block.");
        store.insert(chunk_b, vec![0.0, 1.0, 0.0]);

        // 3. Chunk C: Reverse polar opposite profile (Expected worst score ~-1.0)
        let chunk_c = make_mock_chunk(103, "Inverse opposite vector layout.");
        store.insert(chunk_c, vec![-1.0, 0.0, 0.0]);

        // Execute linear brute force search demanding only top 2 items
        let search_res = store.search(&query_vector, 2);
        assert!(search_res.is_ok());

        let matches = search_res.unwrap();

        // Verify Top_K truncation applied correctly
        assert_eq!(
            matches.len(),
            2,
            "Search loop failed to truncate data down to top_k constraint boundaries."
        );

        // Verify that the unstable sorting mechanism ordered matches from highest score to lowest
        let (ref first_chunk, first_score) = matches[0];
        let (ref second_chunk, second_score) = matches[1];

        assert_eq!(first_chunk.id, 101, "Highest scoring item did not rank first.");
        assert!(first_score > 0.99);

        assert_eq!(second_chunk.id, 102, "Secondary ranked item missed positional slot matching.");
        assert!(second_score < 0.01 && second_score > -0.01);
    }

    #[test]
    fn test_dimension_mismatch_skips_gracefully() {
        let mut store = MemoryVectorStore::new();
        let chunk = make_mock_chunk(200, "Fixed 3-dimension configuration space.");
        store.insert(chunk, vec![0.5, 0.5, 0.5]);

        // Query with an incompatible vector length (2 dimensions instead of 3)
        let broken_query = vec![1.0, 0.0];
        let search_res = store.search(&broken_query, 5).unwrap();

        // The linear pass should filter out/skip vectors that don't match query lengths
        assert!(
            search_res.is_empty(),
            "Search layer processed misaligned lengths instead of skipping them."
        );
    }

    #[test]
    fn test_clear_wipes_all_internal_allocations() {
        let mut store = MemoryVectorStore::new();
        store.insert(make_mock_chunk(1, "Data block A"), vec![1.0, 0.0]);
        store.insert(make_mock_chunk(2, "Data block B"), vec![0.0, 1.0]);

        // Assert items are active
        assert!(store.get_chunk(1).is_some());

        // Wipe internal maps and arrays
        store.clear();

        assert!(store.get_chunk(1).is_none());
        let final_search = store.search(&vec![1.0, 0.0], 5).unwrap();
        assert!(
            final_search.is_empty(),
            "Flat vector storage remained populated after clear routine call."
        );
    }
}
