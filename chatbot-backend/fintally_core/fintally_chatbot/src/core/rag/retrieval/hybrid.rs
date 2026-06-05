use std::collections::HashSet;

pub struct HybridScorer;

impl HybridScorer {
    /// Computes a lean token intersection ratio between the query and documents
    pub fn keyword_score(query: &str, document_text: &str) -> f32 {
        let query_tokens: std::collections::HashSet<String> = query.split_whitespace().map(|w| w.to_lowercase()).collect();
        let doc_tokens: std::collections::HashSet<String> = document_text.split_whitespace().map(|w| w.to_lowercase()).collect();
        
        if query_tokens.is_empty() {
            return 0.0;
        }

        let matches = query_tokens.intersection(&doc_tokens).count();
        matches as f32 / query_tokens.len() as f32
    }

    /// Fuses dense structural distance math against sparse literal matches
    pub fn combine_scores(dense_score: f32, sparse_score: f32, alpha: f32) -> f32 {
        (alpha * dense_score) + ((1.0 - alpha) * sparse_score)
    }
}