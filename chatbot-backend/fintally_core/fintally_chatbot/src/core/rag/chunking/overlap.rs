pub struct OverlapStrategy {
    pub chunk_size: usize,    // Max word count per chunk
    pub chunk_overlap: usize, // Overlapping words between sequential chunks
}

impl OverlapStrategy {
    pub fn new(chunk_size: usize, chunk_overlap: usize) -> Self {
        Self { chunk_size, chunk_overlap }
    }

    /// Divides text using zero-copy token windows to minimize heap churn
    pub fn compute_windows(&self, text: &str) -> Vec<String> {
        let words: Vec<&str> = text.split_whitespace().collect();
        let mut chunks = Vec::new();
        
        if words.is_empty() {
            return chunks;
        }

        let mut start = 0;
        while start < words.len() {
            let end = (start + self.chunk_size).min(words.len());
            let chunk_words = &words[start..end];
            chunks.push(chunk_words.join(" "));
            
            if end == words.len() {
                break;
            }
            
            // Advance window positions safely
            let step = self.chunk_size.saturating_sub(self.chunk_overlap);
            if step == 0 {
                start += 1; // Safeguard against infinite loops if misconfigured
            } else {
                start += step;
            }
        }
        chunks
    }
}