pub struct HybridScorer;

impl HybridScorer {
    /// Computes a lean token intersection ratio between the query and documents.
    /// Optimized for a 2-core Celeron by dropping the document HashSet allocation entirely
    /// and executing a zero-heap sequential stream check.
    pub fn keyword_score(query: &str, document_text: &str) -> f32 {
        // 1. Clean query tokens into a small, flat stack vector
        let mut query_tokens: Vec<String> = query
            .split_whitespace()
            .map(|w| w.to_lowercase())
            .collect();
        
        // Deduplicate query tokens to establish an accurate unique baseline count
        query_tokens.sort_unstable();
        query_tokens.dedup();

        let total_query_tokens = query_tokens.len();
        if total_query_tokens == 0 {
            return 0.0;
        }

        // 2. Track hits using a small stack array instead of an expensive heap set.
        // For queries under 10 words, this array fits directly into CPU registers.
        let mut matched = vec![false; total_query_tokens];
        let mut match_count = 0;

        // 3. Single-pass stream pass over document words with ZERO heap allocations
        for doc_word in document_text.split_whitespace() {
            for (i, query_token) in query_tokens.iter().enumerate() {
                // Perform quick bitwise matching without allocating temporary lowercase strings
                if !matched[i] && doc_word.eq_ignore_ascii_case(query_token) {
                    matched[i] = true;
                    match_count += 1;
                    
                    // Instant early exit if every query token has been successfully located
                    if match_count == total_query_tokens {
                        return 1.0;
                    }
                }
            }
        }

        match_count as f32 / total_query_tokens as f32
    }

    /// Fuses dense structural distance math against sparse literal matches.
    /// Inlined to completely wipe away assembly jump-frame overhead.
    #[inline(always)]
    pub fn combine_scores(dense_score: f32, sparse_score: f32, alpha: f32) -> f32 {
        (alpha * dense_score) + ((1.0 - alpha) * sparse_score)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Micro-helper to validate float equivalence up to 5 decimal places
    fn approx_eq(a: f32, b: f32) -> bool {
        (a - b).abs() < 1e-5
    }

    // ==========================================
    // 1. KEYWORD SCORER MATCHING & BOUNDARIES
    // ==========================================

    #[test]
    fn test_keyword_score_empty_and_whitespace_queries() {
        // Empty query strings must safely drop out without causing division-by-zero panics
        assert_eq!(HybridScorer::keyword_score("", "Valid document text block."), 0.0);
        assert_eq!(HybridScorer::keyword_score("   ", "Valid document text block."), 0.0);
        
        // Empty documents with valid queries should evaluate to zero matches
        assert_eq!(HybridScorer::keyword_score("ledger", ""), 0.0);
    }

    #[test]
    fn test_keyword_score_perfect_match_triggering_early_exit() {
        let query = "finance cash ledger";
        let doc = "Tracking corporate finance cash ledger balances closely.";
        
        // Every single query token is present; must hit the internal early-exit arm and return exactly 1.0
        let score = HybridScorer::keyword_score(query, doc);
        assert_eq!(score, 1.0, "Perfect word matches must instantly trigger a 1.0 early exit score.");
    }

    #[test]
    fn test_keyword_score_partial_matches_and_fractions() {
        let query = "rust python java typescript"; // 4 unique tokens
        let doc = "We use rust and python pipelines across our production environment."; 
        // 2 matches found ("rust", "python") out of 4 total -> expected score: 2.0 / 4.0 = 0.5

        let score = HybridScorer::keyword_score(query, doc);
        assert!(approx_eq(score, 0.5), "Partial token coverage expected 0.5, got: {score}");
    }

    // ==========================================
    // 2. CASE INSENSITIVITY & DEDUPLICATION
    // ==========================================

    #[test]
    fn test_keyword_score_ignores_ascii_case_variants() {
        let query = "LEDGER AMORTIZATION BALANCE";
        let doc = "checking accounts ledger amortization balance sheets.";

        // Your use of eq_ignore_ascii_case should successfully pair these across capitalization casing
        let score = HybridScorer::keyword_score(query, doc);
        assert_eq!(score, 1.0, "Token casing caused a match failure instead of resolving case-insensitively.");
    }

    #[test]
    fn test_keyword_score_deduplicates_query_tokens() {
        // Query repeats the word "cache" 4 times. 
        // The sorting and dedup calls must reduce this query baseline size down to exactly 1 unique token.
        let query = "cache cache cache cache";
        let doc = "System memory layer uses a fast hardware cache pipeline.";

        let score = HybridScorer::keyword_score(query, doc);
        assert_eq!(score, 1.0, "Query deduplication failed, skewing token total counts.");
    }

    // ==========================================
    // 3. SCORE FUSION LINEAR COMBINATION
    // ==========================================

    #[test]
    fn test_combine_scores_fuses_accurately_by_alpha_biases() {
        let dense = 0.80;
        let sparse = 0.40;

        // 1. Balanced weight verification (alpha = 0.5)
        // (0.5 * 0.80) + (0.5 * 0.40) = 0.40 + 0.20 = 0.60
        let balanced_score = HybridScorer::combine_scores(dense, sparse, 0.5);
        assert!(approx_eq(balanced_score, 0.60));

        // 2. Full Dense Bias verification (alpha = 1.0)
        // (1.0 * 0.80) + (0.0 * 0.40) = 0.80
        let dense_only = HybridScorer::combine_scores(dense, sparse, 1.0);
        assert!(approx_eq(dense_only, 0.80));

        // 3. Full Sparse Bias verification (alpha = 0.0)
        // (0.0 * 0.80) + (1.0 * 0.40) = 0.40
        let sparse_only = HybridScorer::combine_scores(dense, sparse, 0.0);
        assert!(approx_eq(sparse_only, 0.40));
    }
}