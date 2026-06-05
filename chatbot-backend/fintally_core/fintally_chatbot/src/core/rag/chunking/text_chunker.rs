use crate::core::rag::errors::RagError;
use super::overlap::OverlapStrategy;

pub struct TextChunker {
    strategy: OverlapStrategy,
}

impl TextChunker {
    pub fn new(chunk_size: usize, chunk_overlap: usize) -> Self {
        Self {
            strategy: OverlapStrategy::new(chunk_size, chunk_overlap),
        }
    }

    pub fn chunk_text(&self, text: &str) -> Result<Vec<String>, RagError> {
        if text.trim().is_empty() {
            return Err(RagError::ChunkingError(
                "Cannot partition completely empty text context structures".into()
            ));
        }
        Ok(self.strategy.compute_windows(text))
    }
}