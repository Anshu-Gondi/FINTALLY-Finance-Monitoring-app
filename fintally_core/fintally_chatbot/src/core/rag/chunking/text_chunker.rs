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

    #[inline]
    pub fn chunk_text(&self, text: &str) -> Result<Vec<Box<str>>, RagError> {
        if text.trim().is_empty() {
            return Err(RagError::ChunkingError(
                "Cannot partition completely empty text context structures".into()
            ));
        }
        Ok(self.strategy.compute_windows(text))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::rag::errors::RagError;

    // ==========================================
    // 1. SUCCESSFUL DELEGATION PATHS
    // ==========================================

    #[test]
    fn test_chunk_text_successful_delegation_to_strategy() {
        // Instantiate chunker with a 2-word size and 1-word overlap scheme
        let chunker = TextChunker::new(2, 1);
        let valid_input = "system memory buffer alignment";

        let result = chunker.chunk_text(valid_input);
        assert!(result.is_ok(), "Chunker threw error on valid text sequence.");

        let chunks = result.unwrap();
        // Expecting: "system memory", "memory buffer", "buffer alignment"
        assert_eq!(chunks.len(), 3, "Window processing delegation failed.");
        assert_eq!(chunks[0].as_ref(), "system memory");
        assert_eq!(chunks[1].as_ref(), "memory buffer");
        assert_eq!(chunks[2].as_ref(), "buffer alignment");
    }

    // ==========================================
    // 2. EMPTY STATE VALIDATION BOUNDARIES
    // ==========================================

    #[test]
    fn test_chunk_text_errors_on_completely_empty_input() {
        let chunker = TextChunker::new(10, 2);
        
        // Pass a totally zero-length string literal
        let result = chunker.chunk_text("");
        assert!(result.is_err(), "Chunker incorrectly allowed processing empty strings.");

        match result.unwrap_err() {
            RagError::ChunkingError(msg) => {
                assert!(msg.contains("Cannot partition completely empty text context structures"));
            }
            other => panic!("Expected RagError::ChunkingError, caught: {:?}", other),
        }
    }

    #[test]
    fn test_chunk_text_errors_on_whitespace_only_input() {
        let chunker = TextChunker::new(5, 1);
        
        // Pass text loaded with nothing but tabs, spacing, and structural newlines
        let bad_input = "   \n\t   \n   ";
        let result = chunker.chunk_text(bad_input);
        
        assert!(
            result.is_err(), 
            "Chunker failed to trap a text block containing only whitespace characters."
        );
    }
}