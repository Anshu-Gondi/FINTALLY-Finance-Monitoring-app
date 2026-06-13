use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct RawDocument {
    pub id: Box<str>,      // Changed from String -> Exact-size allocation, drops stack overhead
    pub content: Box<str>, // Changed from String -> Truncates system allocator "slack" capacity for massive documents
    
    // Changed from HashMap<String, String> -> Packs key-value pairs sequentially 
    // to match the Celeron's L1/L2 cache lines exactly.
    pub metadata: Box<[(Box<str>, Box<str>)]>,
}

impl RawDocument {
    #[inline] // Inlines constructor directly into your ingestion pipeline loops
    pub fn new(id: String, content: String, metadata: HashMap<String, String>) -> Self {
        // Linearly drain the HashMap into an exact-sized contiguous heap block
        let dense_metadata: Box<[(Box<str>, Box<str>)]> = metadata
            .into_iter()
            .map(|(k, v)| (k.into_boxed_str(), v.into_boxed_str()))
            .collect::<Vec<_>>()
            .into_boxed_slice();

        Self {
            id: id.into_boxed_str(),
            content: content.into_boxed_str(),
            metadata: dense_metadata,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    // ==========================================
    // 1. CONSTRUCTOR TRANSFORMATION TESTING
    // ==========================================

    #[test]
    fn test_raw_document_constructor_flattens_allocations() {
        // Instantiate normal runtime Strings and HashMaps
        let input_id = "financial_ledger_2026.pdf".to_string();
        let input_content = "Raw accounting ledger parameters detailing asset balancing.".to_string();
        
        let mut input_metadata = HashMap::new();
        input_metadata.insert("author".to_string(), "Fintally Core".to_string());
        input_metadata.insert("system_tier".to_string(), "production".to_string());

        // Construct the flattened document structure
        let document = RawDocument::new(input_id, input_content, input_metadata);

        // 1. Verify exact-size boxed slices evaluate correctly to text strings
        assert_eq!(document.id.as_ref(), "financial_ledger_2026.pdf");
        assert_eq!(document.content.as_ref(), "Raw accounting ledger parameters detailing asset balancing.");

        // 2. Verify total element conversion lengths match across layouts
        assert_eq!(document.metadata.len(), 2, "Flattened metadata slice lost keys during conversion.");

        // 3. Confirm key/value matching inside the sequential boxed layout
        let author_pair = document.metadata
            .iter()
            .find(|(key, _)| key.as_ref() == "author");
        
        let tier_pair = document.metadata
            .iter()
            .find(|(key, _)| key.as_ref() == "system_tier");

        assert!(author_pair.is_some(), "Metadata loop failed to locate 'author' key variant.");
        assert_eq!(author_pair.unwrap().1.as_ref(), "Fintally Core");

        assert!(tier_pair.is_some(), "Metadata loop failed to locate 'system_tier' key variant.");
        assert_eq!(tier_pair.unwrap().1.as_ref(), "production");
    }

    // ==========================================
    // 2. EMPTY STATE EDGE HANDLING
    // ==========================================

    #[test]
    fn test_empty_inputs_resolve_safely_without_panics() {
        let empty_metadata = HashMap::new();
        
        // Pass empty references to ensure the memory drain loops handle zero sizing cleanly
        let document = RawDocument::new(String::new(), String::new(), empty_metadata);

        assert_eq!(document.id.as_ref(), "");
        assert_eq!(document.content.as_ref(), "");
        assert!(document.metadata.is_empty(), "Empty HashMap conversion produced an active metadata block allocation.");
    }
}