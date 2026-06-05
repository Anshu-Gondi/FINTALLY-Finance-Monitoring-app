use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct IngestionRequest {
    pub file_path: String,
    pub document_type: String, // e.g., "pdf", "txt", "json"
    pub metadata: Option<HashMap<String, String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct IngestionResponse {
    pub document_id: String,
    pub chunks_count: usize,
    pub success: bool,
    pub execution_time_ms: u64,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct QueryRequest {
    pub query: String,
    pub top_k: usize,
    pub score_threshold: Option<f32>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct TextChunkMatch {
    pub chunk_id: u64,
    pub document_id: String,
    pub text: String,
    pub score: f32,
    pub metadata: HashMap<String, String>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct QueryResponse {
    pub context_block: String, // Merged text blocks optimized for LLM consumption
    pub matches: Vec<TextChunkMatch>,
}