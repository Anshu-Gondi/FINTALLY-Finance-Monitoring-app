use std::sync::atomic::{AtomicU64, Ordering};
use crate::core::rag::errors::RagError;
use crate::core::rag::embedding::types::DocumentChunk;
use crate::core::rag::chunking::TextChunker;
use super::loader::DocumentLoader;

// Atomic increments give us cheap, lock-free unique IDs for fast native vector search keys
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

    /// Takes an internal path references, extracts text string maps, and generates RAG chunks
    pub fn process_file(&self, file_path: &str) -> Result<Vec<DocumentChunk>, RagError> {
        let raw_doc = DocumentLoader::load_from_file(file_path)?;
        let string_tokens = self.chunker.chunk_text(&raw_doc.content)?;

        let mut output_chunks = Vec::with_capacity(string_tokens.len());
        
        for (idx, slice_text) in string_tokens.into_iter().enumerate() {
            let unique_id = GLOBAL_CHUNK_COUNTER.fetch_add(1, Ordering::SeqCst);
            
            let mut indexed_metadata = raw_doc.metadata.clone();
            indexed_metadata.insert("chunk_sequence".to_string(), idx.to_string());

            output_chunks.push(DocumentChunk {
                id: unique_id,
                document_id: raw_doc.id.clone(),
                text: slice_text,
                sequence_index: idx,
                metadata: indexed_metadata,
            });
        }

        Ok(output_chunks)
    }
}