use std::collections::HashMap;
use crate::core::rag::embedding::types::DocumentChunk;

pub struct MetadataFilter;

impl MetadataFilter {
    /// Returns true if all provided criteria parameters match the chunk metadata
    pub fn matches(chunk: &DocumentChunk, criteria: &HashMap<String, String>) -> bool {
        for (key, value) in criteria {
            if let Some(chunk_val) = chunk.metadata.get(key) {
                if chunk_val != value {
                    return false;
                }
            } else {
                return false;
            }
        }
        true
    }
}