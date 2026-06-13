use crate::core::rag::embedding::types::DocumentChunk;
use std::fmt::Write; // Brings the zero-allocation write! macro for strings into scope

pub struct ContextFormatter;

impl ContextFormatter {
    /// Compiles all fragments into a single text block.
    /// Optimized for a 2-core Celeron by calculating exact buffer capacity up front
    /// and erasing all loop-level heap allocations.
    pub fn format_context(matches: &[(DocumentChunk, f32)]) -> String {
        if matches.is_empty() {
            return "No background reference facts found matching this query scenario.".to_string();
        }

        // 1. CALCULATE CAPACITY UP FRONT (Exactly ONE Heap Allocation)
        // We calculate the exact text lengths and add an estimated padding overhead 
        // for the reference headers (~120 bytes per node).
        let mut total_estimated_bytes = 100; // For top and bottom banners
        for (chunk, _) in matches {
            total_estimated_bytes += chunk.text.len() + 120;
        }

        let mut block = String::with_capacity(total_estimated_bytes);
        block.push_str("=== EMBEDDED KNOWLEDGE BACKGROUND CONTEXT ===\n");

        // 2. ZERO-ALLOCATION STRING FORMATTING LOOP
        for (i, (chunk, score)) in matches.iter().enumerate() {
            // Linear scan over sequential slice memory to locate the "source_file"
            let source_file = chunk.metadata
                .iter()
                .find(|(k, _)| k.as_ref() == "source_file")
                .map(|(_, v)| v.as_ref())
                .unwrap_or("unknown");

            // Using the write! macro directly on the pre-allocated string block 
            // streams characters straight into the main buffer. 
            // This bypasses the heap allocation cycles completely.
            let _ = write!(
                &mut block,
                "[Reference Node {} | Source: {} | Match Confidence: {:.2}]\nText: {}\n\n",
                i + 1,
                source_file,
                score,
                chunk.text.trim()
            );
        }

        block.push_str("=============================================");
        block
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::rag::embedding::types::DocumentChunk;

    /// Helper utility to spawn mock document chunks for validation checks
    fn make_mock_chunk(text: &str, source_file: Option<&str>) -> DocumentChunk {
        let mut metadata_vec = Vec::new();
        if let Some(src) = source_file {
            metadata_vec.push(("source_file".into(), src.into()));
        }

        DocumentChunk {
            id: 101,
            document_id: "doc_ref_101".into(),
            text: text.into(),
            sequence_index: 0,
            metadata: metadata_vec.into_boxed_slice(),
        }
    }

    // ==========================================
    // 1. EMPTY MATCH BOUNDARY HANDLING
    // ==========================================

    #[test]
    fn test_format_context_returns_graceful_fallback_for_empty_matches() {
        let empty_matches: Vec<(DocumentChunk, f32)> = Vec::new();
        let result = ContextFormatter::format_context(&empty_matches);

        // Verify that empty arrays return the exact structural notification block
        assert_eq!(
            result, 
            "No background reference facts found matching this query scenario."
        );
    }

    // ==========================================
    // 2. END-TO-END STREAM FORMATTING PARITY
    // ==========================================

    #[test]
    fn test_format_context_builds_valid_unified_knowledge_block() {
        // Construct two distinct reference fragments paired with score coordinates
        let chunk_a = make_mock_chunk("Line item asset margin tracking details.", Some("balance_sheet.csv"));
        let chunk_b = make_mock_chunk("   Whitespace padded operational rules constraint string.   ", Some("audit_log.json"));

        let matches = vec![
            (chunk_a, 0.9234),
            (chunk_b, 0.7512),
        ];

        let formatted_output = ContextFormatter::format_context(&matches);

        // 1. Verify global headers and footers are injected
        assert!(formatted_output.starts_with("=== EMBEDDED KNOWLEDGE BACKGROUND CONTEXT ===\n"));
        assert!(formatted_output.ends_with("============================================="));

        // 2. Verify Reference Node 1 metadata resolution and precise 2-decimal score truncation
        assert!(formatted_output.contains("[Reference Node 1 | Source: balance_sheet.csv | Match Confidence: 0.92]"));
        assert!(formatted_output.contains("Text: Line item asset margin tracking details."));

        // 3. Verify Reference Node 2 elements and check that your .trim() hook cleaned trailing padding
        assert!(formatted_output.contains("[Reference Node 2 | Source: audit_log.json | Match Confidence: 0.75]"));
        assert!(formatted_output.contains("Text: Whitespace padded operational rules constraint string."));
    }

    // ==========================================
    // 3. METADATA FALLBACK VALIDATION
    // ==========================================

    #[test]
    fn test_format_context_defaults_gracefully_for_missing_source_metadata() {
        // Build a chunk missing the "source_file" tag completely
        let blind_chunk = make_mock_chunk("Context block text without a source origin link.", None);
        let matches = vec![(blind_chunk, 0.8500)];

        let formatted_output = ContextFormatter::format_context(&matches);

        // The linear scan lookup failure must be safely intercepted by your "unknown" unwrap fallback
        assert!(
            formatted_output.contains("Source: unknown"),
            "Context engine failed to default missing metadata entries to 'unknown'. Output: {}",
            formatted_output
        );
    }
}