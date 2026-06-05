use crate::core::rag::embedding::types::DocumentChunk;

pub struct ContextFormatter;

impl ContextFormatter {
    /// Compiles all fragments into a single text block
    pub fn format_context(matches: &[(DocumentChunk, f32)]) -> String {
        if matches.is_empty() {
            return "No background reference facts found matching this query scenario.".to_string();
        }

        let mut block = String::new();
        block.push_str("=== EMBEDDED KNOWLEDGE BACKGROUND CONTEXT ===\n");

        for (i, (chunk, score)) in matches.iter().enumerate() {
            let source_file = chunk.metadata.get("source_file").map(|s| s.as_str()).unwrap_or("unknown");
            block.push_str(&format!(
                "[Reference Node {} | Source: {} | Match Confidence: {:.2}]\nText: {}\n\n",
                i + 1,
                source_file,
                score,
                chunk.text.trim()
            ));
        }

        block.push_str("=============================================");
        block
    }
}