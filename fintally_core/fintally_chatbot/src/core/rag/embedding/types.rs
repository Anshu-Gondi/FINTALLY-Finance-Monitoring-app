use serde::{Deserialize, Serialize};

/// Represents a raw text split from a document, before vector conversion.
/// Optimized for a 2-core CPU by enforcing strict exact-size allocations
/// and removing heavy hashing infrastructure.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DocumentChunk {
    pub id: u64,               // Globally unique 64-bit ID required for fast USearch indexing
    pub document_id: Box<str>, // Changed from String -> Drops 8-byte capacity tracker and truncates heap slack
    pub text: Box<str>,        // Changed from String -> Exact-size immutable allocation
    pub sequence_index: usize, // Tracks structural order within the original file
    
    // Changed from HashMap<String, String> -> Drops the huge bucket arrays and hashing calculations.
    // For typical RAG chunks with 1-10 metadata items, a flat contiguous boxed slice 
    // minimizes memory footprint and streams directly into L1/L2 CPU caches.
    pub metadata: Box<[(Box<str>, Box<str>)]>,
}

/// Dense floating-point vector wrapper.
/// Stripped down to a boxed slice to minimize memory footprints when thousands
/// of embeddings sit resident in RAM.
#[derive(Debug, Clone)]
pub struct ChunkEmbedding {
    pub chunk_id: u64,
    pub vector: Box<[f32]>, // Changed from Vec<f32> -> Drops structural overhead
}

impl ChunkEmbedding {
    /// Constructs a new ChunkEmbedding, automatically reclaiming unused heap capacity
    #[inline] // Forces code injection at call-site to eliminate function call overhead
    pub fn new(chunk_id: u64, vector: Vec<f32>) -> Self {
        Self { 
            chunk_id, 
            vector: vector.into_boxed_slice() // Instantly truncates system-allocated vector slack
        }
    }

    /// Helper to verify vector health and check dimension alignment
    #[inline]
    pub fn dimension_size(&self) -> usize {
        self.vector.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ==========================================
    // 1. SERDE SERIALIZATION ROUNDTRIP
    // ==========================================

    #[test]
    fn test_document_chunk_serialization_and_deserialization() {
        // Build an exact-sized chunk with sample metadata tags
        let original_chunk = DocumentChunk {
            id: 2026,
            document_id: "financial_report.pdf".into(),
            text: "Net profit margin compressed by macroeconomic headwind vectors.".into(),
            sequence_index: 14,
            metadata: vec![
                ("department".into(), "finance".into()),
                ("confidentiality".into(), "high".into()),
            ].into_boxed_slice(),
        };

        // 1. Serialize the structured exact-size chunk into a JSON text string
        let serialized_json = serde_json::to_string(&original_chunk)
            .expect("Serde failed to handle exact-size boxed slice serialization layouts.");

        // 2. Deserialize the text token sequence back into our memory layout
        let deserialized_chunk: DocumentChunk = serde_json::from_str(&serialized_json)
            .expect("Serde failed to reconstruct exact-size structural fields from JSON payload.");

        // Validate complete field parity
        assert_eq!(deserialized_chunk.id, original_chunk.id);
        assert_eq!(deserialized_chunk.document_id, original_chunk.document_id);
        assert_eq!(deserialized_chunk.text, original_chunk.text);
        assert_eq!(deserialized_chunk.sequence_index, original_chunk.sequence_index);
        
        // Confirm flat array-backed metadata tags persisted completely
        assert_eq!(deserialized_chunk.metadata.len(), original_chunk.metadata.len());
        assert_eq!(deserialized_chunk.metadata[0].0, original_chunk.metadata[0].0);
        assert_eq!(deserialized_chunk.metadata[0].1, original_chunk.metadata[0].1);
    }

    // ==========================================
    // 2. EMBEDDING SLACK TRUNCATION VERIFICATION
    // ==========================================

    #[test]
    fn test_chunk_embedding_new_truncates_vector_capacity_slack() {
        // Explicitly allocate an over-sized vector with massive capacity padding
        let excess_capacity = 128;
        let mut raw_vector = Vec::with_capacity(excess_capacity);
        raw_vector.push(0.15);
        raw_vector.push(-0.82);
        raw_vector.push(0.94);

        // Prove the vector currently carries unneeded structural memory overhead
        assert!(raw_vector.capacity() >= excess_capacity);
        assert_eq!(raw_vector.len(), 3);

        // Instantiating triggers into_boxed_slice() to strip out allocator capacity flags
        let embedding = ChunkEmbedding::new(999, raw_vector);

        // Validate length properties match our data coordinates perfectly
        assert_eq!(embedding.dimension_size(), 3, "Embedding dimension shifted during boxing.");
        assert_eq!(embedding.vector.as_ref(), &[0.15, -0.82, 0.94]);
    }

    #[test]
    fn test_empty_embedding_dimension_handling() {
        // Verify empty embedding arrays pass dimensions limits safely without causing faults
        let empty_embedding = ChunkEmbedding::new(0, Vec::new());
        assert_eq!(empty_embedding.dimension_size(), 0);
    }
}