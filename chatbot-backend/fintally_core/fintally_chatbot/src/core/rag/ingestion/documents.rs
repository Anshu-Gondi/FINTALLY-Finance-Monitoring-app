use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct RawDocument {
    pub id: String,            // Unique identifier for the document, e.g., a UUID or file path
    pub content: String,          // The full raw text content of the document
    pub metadata: HashMap<String, String>, // Optional key-value pairs for additional context (e.g., author, date)
}

impl RawDocument {
    pub fn new(id: String, content: String, metadata: HashMap<String, String>) -> Self {
        Self { id, content, metadata }
    }
}