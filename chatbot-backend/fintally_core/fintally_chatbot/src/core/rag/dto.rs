use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct IngestionRequest {
    // CELERON TUNING: Box<str> drops stack allocation from 24 bytes to 16 bytes 
    // by completely eliminating the unused capacity field.
    pub file_path: Box<str>,
    pub document_type: Box<str>, 
    
    // Swapping HashMap for BTreeMap packs metadata contiguously into small node arrays,
    // avoiding the massive allocation overhead of sparse hash tables.
    pub metadata: Option<BTreeMap<Box<str>, Box<str>>>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct IngestionResponse {
    pub document_id: Box<str>,
    pub chunks_count: usize,
    pub success: bool,
    pub execution_time_ms: u64,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct QueryRequest {
    pub query: Box<str>,
    pub top_k: usize,
    pub score_threshold: Option<f32>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct TextChunkMatch {
    pub chunk_id: u64,
    pub document_id: Box<str>,
    pub text: Box<str>, // Prevents the heap from holding bloated, over-allocated string capacity buffers
    pub score: f32,
    pub metadata: BTreeMap<Box<str>, Box<str>>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct QueryResponse {
    // When merging massive context text blocks for LLMs, Box<str> locks down the final memory
    // allocation, freeing up the excess capacity padding used during string construction.
    pub context_block: Box<str>, 
    pub matches: Vec<TextChunkMatch>,
}

// ==========================================
// UNIT TESTS FOR DTO SERIALIZATION BOUNDARIES
// ==========================================
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ingestion_request_serialization_roundtrip() {
        let mut metadata = BTreeMap::new();
        metadata.insert("department".into(), "finance".into());
        metadata.insert("confidential".into(), "true".into());

        let original = IngestionRequest {
            file_path: "secure/docs/q4_report.pdf".into(),
            document_type: "pdf".into(),
            metadata: Some(metadata),
        };

        // Serialize to JSON string footprint
        let serialized = serde_json::to_string(&original).expect("Failed serialization");
        
        // Deserialize back into structured memory architecture
        let deserialized: IngestionRequest = serde_json::from_str(&serialized).expect("Failed deserialization");

        assert_eq!(deserialized.file_path, original.file_path);
        assert_eq!(deserialized.document_type, original.document_type);
        
        let extracted_metadata = deserialized.metadata.unwrap();
        assert_eq!(extracted_metadata.get("department").map(|s| s.as_ref()), Some("finance"));
        assert_eq!(extracted_metadata.get("confidential").map(|s| s.as_ref()), Some("true"));
    }

    #[test]
    fn test_ingestion_response_layout() {
        let response = IngestionResponse {
            document_id: "doc_hash_99812".into(),
            chunks_count: 42,
            success: true,
            execution_time_ms: 124,
        };

        let serialized = serde_json::to_string(&response).unwrap();
        let deserialized: IngestionResponse = serde_json::from_str(&serialized).unwrap();

        assert_eq!(deserialized.document_id, response.document_id);
        assert_eq!(deserialized.chunks_count, response.chunks_count);
        assert_eq!(deserialized.success, response.success);
        assert_eq!(deserialized.execution_time_ms, response.execution_time_ms);
    }

    #[test]
    fn test_query_request_optional_thresholds() {
        // Case 1: Testing layout passing a clean score threshold
        let req_with_threshold = QueryRequest {
            query: "What is our burn rate?".into(),
            top_k: 5,
            score_threshold: Some(0.75),
        };
        let serialized = serde_json::to_string(&req_with_threshold).unwrap();
        let deserialized: QueryRequest = serde_json::from_str(&serialized).unwrap();
        assert_eq!(deserialized.score_threshold, Some(0.75));

        // Case 2: Testing fallback options maps safely to None
        let req_without_threshold = QueryRequest {
            query: "Clearance codes".into(),
            top_k: 1,
            score_threshold: None,
        };
        let serialized_none = serde_json::to_string(&req_without_threshold).unwrap();
        let deserialized_none: QueryRequest = serde_json::from_str(&serialized_none).unwrap();
        assert!(deserialized_none.score_threshold.is_none());
    }

    #[test]
    fn test_query_response_complex_nested_structures() {
        let mut match_metadata = BTreeMap::new();
        match_metadata.insert("author".into(), "system_agent".into());

        let chunk_match = TextChunkMatch {
            chunk_id: 1024,
            document_id: "source_doc_01".into(),
            text: "Isolated vectorized slice block context content.".into(),
            score: 0.925,
            metadata: match_metadata,
        };

        let original_response = QueryResponse {
            context_block: "Consolidated massive context block ready for immediate LLM injection injection context...".into(),
            matches: vec![chunk_match],
        };

        let serialized = serde_json::to_string(&original_response).unwrap();
        let deserialized: QueryResponse = serde_json::from_str(&serialized).unwrap();

        assert_eq!(deserialized.context_block, original_response.context_block);
        assert_eq!(deserialized.matches.len(), 1);
        assert_eq!(deserialized.matches[0].chunk_id, 1024);
        assert_eq!(deserialized.matches[0].score, 0.925);
        assert_eq!(
            deserialized.matches[0].metadata.get("author").unwrap().as_ref(),
            "system_agent"
        );
    }
}