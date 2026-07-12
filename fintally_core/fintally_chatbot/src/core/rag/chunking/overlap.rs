
pub struct OverlapStrategy {
    pub chunk_size: usize,    // Max word count per chunk
    pub chunk_overlap: usize, // Overlapping words between sequential chunks
}

impl OverlapStrategy {
    pub fn new(chunk_size: usize, chunk_overlap: usize) -> Self {
        Self { chunk_size, chunk_overlap }
    }

    /// Divides text using zero-copy token windows to minimize heap churn.
    /// Returns exact-sized Box<str> slices to eliminate capacity padding.
    pub fn compute_windows(&self, text: &str) -> Vec<Box<str>> {
        // 1. Scan byte indices to collect word start/end boundaries.
        // A pair of numbers (usize, usize) uses half the memory of an explicit string slice pointer setup.
        let mut word_bounds = Vec::with_capacity(text.len() / 6); // Optimal pre-allocation guess
        let mut start_idx = None;

        for (idx, &byte) in text.as_bytes().iter().enumerate() {
            if byte == b' ' {
                if let Some(start) = start_idx {
                    word_bounds.push((start, idx));
                    start_idx = None;
                }
            } else if start_idx.is_none() {
                start_idx = Some(idx);
            }
        }
        if let Some(start) = start_idx {
            word_bounds.push((start, text.len()));
        }

        let num_words = word_bounds.len();
        if num_words == 0 {
            return Vec::new();
        }

        let mut chunks = Vec::new();
        let mut start = 0;

        // 2. Sliding Window Calculation
        while start < num_words {
            let end = (start + self.chunk_size).min(num_words);
            
            // Extract exact slice bounds directly out of our boundary cache
            let chunk_start_byte = word_bounds[start].0;
            let chunk_end_byte = word_bounds[end - 1].1;
            
            // Slice the source document directly with zero-copy overhead, then box it cleanly
            let chunk_slice = &text[chunk_start_byte..chunk_end_byte];
            chunks.push(chunk_slice.into());
            
            if end == num_words {
                break;
            }
            
            // Advance window positions safely
            let step = self.chunk_size.saturating_sub(self.chunk_overlap);
            if step == 0 {
                start += 1;
            } else {
                start += step;
            }
        }
        chunks
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ==========================================
    // 1. STANDARD SLIDING WINDOW DISTRIBUTION
    // ==========================================

    #[test]
    fn test_compute_windows_creates_correct_overlapping_slices() {
        // Strategy: 3 words per chunk, 1 word overlap
        let strategy = OverlapStrategy::new(3, 1);
        let sample_text = "alpha beta gamma delta epsilon zeta";
        
        let chunks = strategy.compute_windows(sample_text);

        // Expected progression:
        // Window 1: "alpha beta gamma" (Words 0, 1, 2)
        // Step size = 3 - 1 = 2. Next window starts at index 2 (gamma)
        // Window 2: "gamma delta epsilon" (Words 2, 3, 4)
        // Next window starts at index 4 (epsilon)
        // Window 3: "epsilon zeta" (Words 4, 5)
        assert_eq!(chunks.len(), 3, "Sliding window distribution failed to capture all phrases.");
        
        assert_eq!(chunks[0].as_ref(), "alpha beta gamma");
        assert_eq!(chunks[1].as_ref(), "gamma delta epsilon");
        assert_eq!(chunks[2].as_ref(), "epsilon zeta");
    }

    // ==========================================
    // 2. BOUNDARY CONDITIONS & OVERSIZED VALUES
    // ==========================================

    #[test]
    fn test_text_shorter_than_chunk_size_produces_single_chunk() {
        let strategy = OverlapStrategy::new(10, 2);
        let sample_text = "small asset ledger";

        let chunks = strategy.compute_windows(sample_text);

        assert_eq!(chunks.len(), 1, "Short text block shouldn't split into multiple intervals.");
        assert_eq!(chunks[0].as_ref(), "small asset ledger");
    }

    #[test]
    fn test_empty_text_returns_empty_vector() {
        let strategy = OverlapStrategy::new(5, 1);
        let chunks = strategy.compute_windows("");

        assert!(chunks.is_empty(), "Passing an empty text buffer must yield an empty chunk array.");
    }

    // ==========================================
    // 3. MISCONFIGURATION INVARIANT SAFEGUARDS
    // ==========================================

    #[test]
    fn test_pathological_overlap_values_do_not_infinite_loop() {
        // Edge Case: Overlap size is configured to equal or exceed total chunk size
        // This makes step = 0. Your system must advance by 1 to prevent hanging the thread.
        let strategy = OverlapStrategy::new(2, 2);
        let sample_text = "one two three four";

        let chunks = strategy.compute_windows(sample_text);

        // Progressing safely by 1 word at a time instead of hanging:
        // Chunk 0: "one two"
        // Chunk 1: "two three"
        // Chunk 2: "three four"
        assert_eq!(chunks.len(), 3, "Infinite loop safeguard step failed to calculate correctly.");
        assert_eq!(chunks[0].as_ref(), "one two");
        assert_eq!(chunks[1].as_ref(), "two three");
        assert_eq!(chunks[2].as_ref(), "three four");
    }
}