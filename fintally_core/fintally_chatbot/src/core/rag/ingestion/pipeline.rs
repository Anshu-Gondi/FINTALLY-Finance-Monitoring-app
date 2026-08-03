use std::sync::atomic::{AtomicU64, Ordering};
use crate::core::rag::errors::RagError;
use crate::core::rag::embedding::types::DocumentChunk;
use crate::core::rag::chunking::TextChunker;
use super::loader::DocumentLoader;

// CELERON TUNING: Switched to Relaxed atomic ordering below. 
// We only need a unique incrementing number; we don't need a heavy multi-core memory fence.
static GLOBAL_CHUNK_COUNTER: AtomicU64 = AtomicU64::new(1);

pub struct IngestionPipeline {
    chunker: TextChunker,
}

impl IngestionPipeline {
    pub fn new(chunk_size: usize, chunk_overlap: usize) -> Self {
        Self {
            chunker: TextChunker::new(chunk_size, chunk_overlap),
        }
    }

    /// Takes internal path references, extracts text strings, and generates optimized RAG chunks.
    pub fn process_file(&self, file_path: &str) -> Result<Vec<DocumentChunk>, RagError> {
        let raw_doc = DocumentLoader::load_from_file(file_path)?;
        
        // chunk_text now natively returns Vec<Box<str>> based on our zero-copy string upgrades
        let string_tokens = self.chunker.chunk_text(&raw_doc.content)?;

        let mut output_chunks = Vec::with_capacity(string_tokens.len());

        // 1. PRE-CONVERT BASE METADATA ONCE
        // Instead of parsing the raw map inside the loop, extract and box the immutable 
        // global file metadata once. This cuts down on string parsing iterations.
        let base_metadata: Vec<(Box<str>, Box<str>)> = raw_doc.metadata
            .iter()
            .map(|(k, v)| (k.as_ref().into(), v.as_ref().into()))
            .collect();

        for (idx, slice_text) in string_tokens.into_iter().enumerate() {
            // 2. USE LIGHTWEIGHT ATOMIC ORDERING
            // Relaxed guarantees uniqueness without triggering expensive CPU cache invalidation cycles.
            let unique_id = GLOBAL_CHUNK_COUNTER.fetch_add(1, Ordering::Relaxed);
            
            // 3. LINEAR MEMORY METADATA BUILD
            // Allocate the exact slice capacity upfront (base items + 1 for the sequence index)
            let mut chunk_metadata = Vec::with_capacity(base_metadata.len() + 1);
            
            // Copy the pre-boxed base references sequentially
            for (k, v) in &base_metadata {
                chunk_metadata.push((k.clone(), v.clone()));
            }
            
            // Append the tracking sequence slice cleanly without a heavy HashMap re-hash step
            let sequence_key: Box<str> = "chunk_sequence".into();
            let sequence_val: Box<str> = idx.to_string().into_boxed_str();
            chunk_metadata.push((sequence_key, sequence_val));

            output_chunks.push(DocumentChunk {
                id: unique_id,
                document_id: raw_doc.id.clone(),
                text: slice_text, // Matches the optimized Vec<Box<str>> layout
                sequence_index: idx,
                metadata: chunk_metadata.into_boxed_slice(), // Finalizes the flat, cache-friendly array
            });
        }

        Ok(output_chunks)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::env;
    use std::path::PathBuf;
    use std::hash::{Hash, Hasher};
    use std::collections::hash_map::DefaultHasher;

    /// Helper for unique test file naming
    fn create_temporary_test_file(filename: &str, text_content: &str) -> PathBuf {
        let mut path = env::temp_dir();
        // Use a simple time/thread-based hash for uniqueness
        let mut hasher = DefaultHasher::new();
        std::thread::current().id().hash(&mut hasher);
        std::time::Instant::now().hash(&mut hasher);
        
        path.push(format!("fintally_{}_{}_{}", hasher.finish(), std::process::id(), filename));
        fs::write(&path, text_content).expect("Failed to write temp test file");
        path
    }

    // ==========================================
    // 1. PIPELINE EXECUTION & ATOMIC ATTESTATION
    // ==========================================

    #[test]
    fn test_ingestion_pipeline_atomic_uniqueness_and_sequence_tracking() {
        // Fix: Provide required arguments (chunk_size: usize, overlap: usize)
        let pipeline = IngestionPipeline::new(10, 2);
        
        let sample_text = "Line item ledger asset tracking rules. Capital checking validation procedures. Operational buffer matrix values.";
        let temp_file = create_temporary_test_file("atomic_test.txt", sample_text);

        let result = pipeline.process_file(temp_file.to_str().unwrap());
        assert!(result.is_ok(), "Pipeline processing failed: {:?}", result.err());

        let chunks = result.unwrap();
        assert!(chunks.len() > 1, "Text did not split into separate chunk items.");

        let mut previous_id = 0u64;
        
        for (idx, chunk) in chunks.iter().enumerate() {
            assert!(chunk.id > previous_id, "Atomic chunk ID assignment was not strictly monotonic.");
            previous_id = chunk.id;
            assert_eq!(chunk.sequence_index, idx, "Chunk sequence field misaligned from execution loop state.");
        }

        let _ = fs::remove_file(temp_file);
    }

    // ==========================================
    // 2. METADATA BOXED ARRAYS FLATTENING
    // ==========================================

    #[test]
    fn test_metadata_flattening_and_sequence_injection() {
        // Fix: Provide required arguments
        let pipeline = IngestionPipeline::new(50, 5);
        let sample_text = "Single record evaluation string token layout.";
        let temp_file = create_temporary_test_file("metadata_test.txt", sample_text);

        let chunks = pipeline.process_file(temp_file.to_str().unwrap()).unwrap();
        assert!(!chunks.is_empty());

        let target_chunk = &chunks[0];

        // Locating our injected tracking key
        let sequence_tag = target_chunk.metadata
            .iter()
            .find(|(k, _)| k.as_ref() == "chunk_sequence");

        assert!(sequence_tag.is_some(), "Pipeline failed to inject 'chunk_sequence' marker.");
        assert_eq!(sequence_tag.unwrap().1.as_ref(), "0", "Injected index sequence value failed validation.");

        let _ = fs::remove_file(temp_file);
    }

    // ==========================================
    // 3. STORAGE DISK FAULT ISOLATION
    // ==========================================

    #[test]
    fn test_pipeline_bubbles_missing_file_errors() {
        // Fix: Provide required arguments
        let pipeline = IngestionPipeline::new(100, 10);
        
        let missing_path = "non_existent_fintally_system_file.txt";
        let execution_run = pipeline.process_file(missing_path);

        assert!(
            execution_run.is_err(),
            "Pipeline successfully processed data from a non-existent file system path."
        );
    }
}