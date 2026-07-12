use std::collections::HashMap;
use crate::core::rag::embedding::types::DocumentChunk;

pub struct MetadataFilter;

impl MetadataFilter {
    /// Returns true if all provided criteria parameters match the chunk metadata.
    /// Bypasses hashing calculations for a cache-friendly sequential slice scan.
    #[inline]
    pub fn matches(chunk: &DocumentChunk, criteria: &HashMap<String, String>) -> bool {
        // Early exit: If criteria filter requires more keys than the chunk possesses,
        // it cannot possibly be a complete match.
        if criteria.len() > chunk.metadata.len() {
            return false;
        }

        for (key, value) in criteria {
            // Linear scan over sequential memory buffer.
            // Compares the underlying &str slices directly without allocating memory.
            let chunk_entry = chunk.metadata
                .iter()
                .find(|(chunk_key, _)| chunk_key.as_ref() == key.as_str());

            match chunk_entry {
                Some((_, chunk_val)) => {
                    if chunk_val.as_ref() != value.as_str() {
                        return false; // Key matches, but value is a mismatch
                    }
                }
                None => return false, // The required filter key does not exist in this chunk
            }
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::rag::embedding::types::DocumentChunk;

    /// Helper to instantiate a document chunk with a predefined metadata block.
    /// Updated to match the strict schema fields required by the current project build.
    fn make_chunk_with_metadata(metadata_map: std::collections::BTreeMap<String, String>) -> DocumentChunk {
        // Convert BTreeMap to the Boxed slice-of-pairs format required by your struct
        let metadata: Box<[(Box<str>, Box<str>)]> = metadata_map
            .into_iter()
            .map(|(k, v)| (k.into_boxed_str(), v.into_boxed_str()))
            .collect();

        DocumentChunk {
            id: 1,
            document_id: "default_doc".into(),
            sequence_index: 0,
            text: "Context block for validation rules.".into(),
            metadata,
        }
    }

    // ==========================================
    // 1. EARLY EXIT & VACUOUS TRUTH
    // ==========================================

    #[test]
    fn test_empty_criteria_matches_everything() {
        let mut metadata = std::collections::BTreeMap::new();
        metadata.insert("category".to_string(), "finance".to_string());
        let chunk = make_chunk_with_metadata(metadata);

        let criteria = std::collections::HashMap::new();
        assert!(MetadataFilter::matches(&chunk, &criteria), "Empty criteria failed to pass.");
    }

    #[test]
    fn test_early_exit_when_criteria_outnumbers_metadata() {
        let mut metadata = std::collections::BTreeMap::new();
        metadata.insert("source".to_string(), "ledger.pdf".to_string());
        let chunk = make_chunk_with_metadata(metadata);

        let mut criteria = std::collections::HashMap::new();
        criteria.insert("source".to_string(), "ledger.pdf".to_string());
        criteria.insert("year".to_string(), "2026".to_string());

        assert!(!MetadataFilter::matches(&chunk, &criteria), "Length validation guardrail failed to reject query.");
    }

    // ==========================================
    // 2. KEY/VALUE ACCURACY TESTING
    // ==========================================

    #[test]
    fn test_perfect_metadata_match() {
        let mut metadata = std::collections::BTreeMap::new();
        metadata.insert("department".to_string(), "accounting".to_string());
        metadata.insert("security_level".to_string(), "L2".to_string());
        let chunk = make_chunk_with_metadata(metadata);

        let mut criteria = std::collections::HashMap::new();
        criteria.insert("department".to_string(), "accounting".to_string());
        criteria.insert("security_level".to_string(), "L2".to_string());

        assert!(MetadataFilter::matches(&chunk, &criteria), "Valid matching metadata tags were rejected.");
    }

    #[test]
    fn test_key_exists_but_value_is_mismatched() {
        let mut metadata = std::collections::BTreeMap::new();
        metadata.insert("environment".to_string(), "production".to_string());
        let chunk = make_chunk_with_metadata(metadata);

        let mut criteria = std::collections::HashMap::new();
        criteria.insert("environment".to_string(), "staging".to_string());

        assert!(!MetadataFilter::matches(&chunk, &criteria), "System allowed value mismatch pass.");
    }

    #[test]
    fn test_required_filter_key_missing_entirely() {
        let mut metadata = std::collections::BTreeMap::new();
        metadata.insert("author".to_string(), "alice".to_string());
        let chunk = make_chunk_with_metadata(metadata);

        let mut criteria = std::collections::HashMap::new();
        criteria.insert("tenant_id".to_string(), "alice".to_string());

        assert!(!MetadataFilter::matches(&chunk, &criteria), "System failed to detect missing filter key criteria.");
    }
}