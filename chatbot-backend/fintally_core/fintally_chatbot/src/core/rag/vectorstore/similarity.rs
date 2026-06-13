pub struct Similarity;

impl Similarity {
    /// Public entry point. Performs quick sanity checks and invokes runtime dispatching 
    /// to choose the best physical SIMD layer available on the host processor.
    pub fn cosine_similarity(a: &[f32], b: &[f32]) -> f32 {
        if a.len() != b.len() || a.is_empty() {
            return 0.0;
        }

        // 1. RUNTIME CPU DISPATCHING
        // Evaluates hardware flags from top-tier down to baseline architectures.
        let (dot_sum, mag_a_sq, mag_b_sq) = unsafe {
            if is_x86_feature_detected!("avx512f") {
                Self::fused_avx512(a, b)
            } else if is_x86_feature_detected!("avx2") && is_x86_feature_detected!("fma") {
                Self::fused_avx2(a, b)
            } else if is_x86_feature_detected!("sse4.2") {
                Self::fused_sse42(a, b)
            } else {
                Self::fused_scalar_fallback(a, b)
            }
        };

        if mag_a_sq <= 0.0 || mag_b_sq <= 0.0 {
            return 0.0;
        }

        dot_sum / (mag_a_sq * mag_b_sq).sqrt()
    }

    // ==========================================
    // EXTRACTION LAYER 1: AVX-512 (16-wide F32 lanes)
    // ==========================================
    #[target_feature(enable = "avx512f")]
    unsafe fn fused_avx512(a: &[f32], b: &[f32]) -> (f32, f32, f32) {
        use std::arch::x86_64::*;

        let mut dot = _mm512_setzero_ps();
        let mut ma = _mm512_setzero_ps();
        let mut mb = _mm512_setzero_ps();

        let len = a.len();
        let rem = len % 16;
        let main_len = len - rem;

        // Process 16 floats simultaneously per instruction cycle
        let mut i = 0;
        while i < main_len {
            let va = _mm512_loadu_ps(a.as_ptr().add(i));
            let vb = _mm512_loadu_ps(b.as_ptr().add(i));

            dot = _mm512_fmadd_ps(va, vb, dot);
            ma = _mm512_fmadd_ps(va, va, ma);
            mb = _mm512_fmadd_ps(vb, vb, mb);
            i += 16;
        }

        // Fast horizontal reduction across the 512-bit vector pipelines
        let mut dot_sum = _mm512_reduce_add_ps(dot);
        let mut mag_a_sq = _mm512_reduce_add_ps(ma);
        let mut mag_b_sq = _mm512_reduce_add_ps(mb);

        // Scalar cleanup loop for remaining elements
        while i < len {
            let va = *a.get_unchecked(i);
            let vb = *b.get_unchecked(i);
            dot_sum += va * vb;
            mag_a_sq += va * va;
            mag_b_sq += vb * vb;
            i += 1;
        }

        (dot_sum, mag_a_sq, mag_b_sq)
    }

    // ==========================================
    // EXTRACTION LAYER 2: AVX2 + FMA (8-wide F32 lanes)
    // ==========================================
    #[target_feature(enable = "avx2,fma")]
    unsafe fn fused_avx2(a: &[f32], b: &[f32]) -> (f32, f32, f32) {
        use std::arch::x86_64::*;

        let mut dot = _mm256_setzero_ps();
        let mut ma = _mm256_setzero_ps();
        let mut mb = _mm256_setzero_ps();

        let len = a.len();
        let rem = len % 8;
        let main_len = len - rem;

        let mut i = 0;
        while i < main_len {
            let va = _mm256_loadu_ps(a.as_ptr().add(i));
            let vb = _mm256_loadu_ps(b.as_ptr().add(i));

            // Fused Multiply-Add handles (va * vb) + dot in a single CPU tick
            dot = _mm256_fmadd_ps(va, vb, dot);
            ma = _mm256_fmadd_ps(va, va, ma);
            mb = _mm256_fmadd_ps(vb, vb, mb);
            i += 8;
        }

        // Horizontal addition using intermediate stack allocations
        let mut buf_dot = [0.0f32; 8];
        let mut buf_ma = [0.0f32; 8];
        let mut buf_mb = [0.0f32; 8];

        _mm256_storeu_ps(buf_dot.as_mut_ptr(), dot);
        _mm256_storeu_ps(buf_ma.as_mut_ptr(), ma);
        _mm256_storeu_ps(buf_mb.as_mut_ptr(), mb);

        let mut dot_sum: f32 = buf_dot.iter().sum();
        let mut mag_a_sq: f32 = buf_ma.iter().sum();
        let mut mag_b_sq: f32 = buf_mb.iter().sum();

        while i < len {
            let va = *a.get_unchecked(i);
            let vb = *b.get_unchecked(i);
            dot_sum += va * vb;
            mag_a_sq += va * va;
            mag_b_sq += vb * vb;
            i += 1;
        }

        (dot_sum, mag_a_sq, mag_b_sq)
    }

    // ==========================================
    // EXTRACTION LAYER 3: SSE4.2 (4-wide F32 lanes)
    // ==========================================
    #[target_feature(enable = "sse4.2")]
    unsafe fn fused_sse42(a: &[f32], b: &[f32]) -> (f32, f32, f32) {
        use std::arch::x86_64::*;

        let mut dot = _mm_setzero_ps();
        let mut ma = _mm_setzero_ps();
        let mut mb = _mm_setzero_ps();

        let len = a.len();
        let rem = len % 4;
        let main_len = len - rem;

        let mut i = 0;
        while i < main_len {
            let va = _mm_loadu_ps(a.as_ptr().add(i));
            let vb = _mm_loadu_ps(b.as_ptr().add(i));

            // Legacy architectures use separate mult and add units
            dot = _mm_add_ps(dot, _mm_mul_ps(va, vb));
            ma = _mm_add_ps(ma, _mm_mul_ps(va, va));
            mb = _mm_add_ps(mb, _mm_mul_ps(vb, vb));
            i += 4;
        }

        let mut buf_dot = [0.0f32; 4];
        let mut buf_ma = [0.0f32; 4];
        let mut buf_mb = [0.0f32; 4];

        _mm_storeu_ps(buf_dot.as_mut_ptr(), dot);
        _mm_storeu_ps(buf_ma.as_mut_ptr(), ma);
        _mm_storeu_ps(buf_mb.as_mut_ptr(), mb);

        let mut dot_sum: f32 = buf_dot.iter().sum();
        let mut mag_a_sq: f32 = buf_ma.iter().sum();
        let mut mag_b_sq: f32 = buf_mb.iter().sum();

        while i < len {
            let va = *a.get_unchecked(i);
            let vb = *b.get_unchecked(i);
            dot_sum += va * vb;
            mag_a_sq += va * va;
            mag_b_sq += vb * vb;
            i += 1;
        }

        (dot_sum, mag_a_sq, mag_b_sq)
    }

    // ==========================================
    // BACKUP LAYER: Safe Unrolled Scalar Fallback
    // ==========================================
    unsafe fn fused_scalar_fallback(a: &[f32], b: &[f32]) -> (f32, f32, f32) {
        let mut chunks_a = a.chunks_exact(4);
        let mut chunks_b = b.chunks_exact(4);

        let mut dot_sum = 0.0;
        let mut mag_a_sq = 0.0;
        let mut mag_b_sq = 0.0;

        for (ca, cb) in chunks_a.by_ref().zip(chunks_b.by_ref()) {
            dot_sum += ca[0] * cb[0] + ca[1] * cb[1] + ca[2] * cb[2] + ca[3] * cb[3];
            mag_a_sq += ca[0] * ca[0] + ca[1] * ca[1] + ca[2] * ca[2] + ca[3] * ca[3];
            mag_b_sq += cb[0] * cb[0] + cb[1] * cb[1] + cb[2] * cb[2] + cb[3] * cb[3];
        }

        for (&x, &y) in chunks_a.remainder().iter().zip(chunks_b.remainder()) {
            dot_sum += x * y;
            mag_a_sq += x * x;
            mag_b_sq += y * y;
        }

        (dot_sum, mag_a_sq, mag_b_sq)
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
    // 1. HARDWARE ENVIRONMENT DIAGNOSTIC LOG
    // ==========================================
    #[test]
    fn test_hardware_diagnostic_printout() {
        // Run with `cargo test -- --nocapture` to see what path your chip uses!
        println!("\n=== HOST CPU DETECTED ARCHITECTURAL LAYERS ===");
        println!("  -> AVX-512 Support : {}", is_x86_feature_detected!("avx512f"));
        println!("  -> AVX2+FMA Support: {}", is_x86_feature_detected!("avx2") && is_x86_feature_detected!("fma"));
        println!("  -> SSE4.2 Support  : {}", is_x86_feature_detected!("sse4.2"));
        println!("==============================================\n");
    }

    // ==========================================
    // 2. MATHEMATICAL BOUNDARY INVARIANCE TESTS
    // ==========================================
    #[test]
    fn test_cosine_similarity_identical_vectors() {
        let vec_a = vec![1.0, 2.0, 3.0, 4.0];
        let vec_b = vec![1.0, 2.0, 3.0, 4.0];
        
        let score = Similarity::cosine_similarity(&vec_a, &vec_b);
        assert!(approx_eq(score, 1.0), "Identical vectors must return 1.0, got: {score}");
    }

    #[test]
    fn test_cosine_similarity_orthogonal_vectors() {
        let vec_a = vec![1.0, 0.0, 0.0, 0.0];
        let vec_b = vec![0.0, 1.0, 0.0, 0.0];
        
        let score = Similarity::cosine_similarity(&vec_a, &vec_b);
        assert!(approx_eq(score, 0.0), "Orthogonal vectors must return 0.0, got: {score}");
    }

    #[test]
    fn test_cosine_similarity_inverse_vectors() {
        let vec_a = vec![2.0, -4.0, 5.5];
        let vec_b = vec![-2.0, 4.0, -5.5];
        
        let score = Similarity::cosine_similarity(&vec_a, &vec_b);
        assert!(approx_eq(score, -1.0), "Exactly reversed vectors must return -1.0, got: {score}");
    }

    #[test]
    fn test_safety_guardrails_on_empty_and_mismatched() {
        // Size mismatch check
        assert_eq!(Similarity::cosine_similarity(&[1.0, 2.0], &[1.0, 2.0, 3.0]), 0.0);
        
        // Cold empty slice check
        assert_eq!(Similarity::cosine_similarity(&[], &[]), 0.0);
        
        // Zero division / Zero magnitude check
        assert_eq!(Similarity::cosine_similarity(&[0.0, 0.0], &[1.0, 5.0]), 0.0);
    }

    // ==========================================
    // 3. VECTOR STRIDE LOOP BOUNDARY TESTING
    // ==========================================
    #[test]
    fn test_loop_remainders_across_all_strides() {
        // Length 3: Forces execution entirely inside scalar loops or remainder arms (< 4 elements)
        let a_3 = vec![1.5, 2.5, 3.5];
        let b_3 = vec![1.5, 2.5, 3.5];
        assert!(approx_eq(Similarity::cosine_similarity(&a_3, &b_3), 1.0));

        // Length 7: Hits SSE (stride 4) + 3 elements of cleanup, or unrolled fallback chunk remainders
        let a_7 = vec![1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0];
        let b_7 = vec![1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0];
        assert!(approx_eq(Similarity::cosine_similarity(&a_7, &b_7), 1.0));

        // Length 19: Large odd length to verify multi-lane vector blocks cleanly dump into standard cleanups
        let a_19: Vec<f32> = (0..19).map(|i| i as f32).collect();
        let b_19: Vec<f32> = (0..19).map(|i| i as f32).collect();
        assert!(approx_eq(Similarity::cosine_similarity(&a_19, &b_19), 1.0));
    }
}