use crate::core::rag::errors::RagError;

pub const BLOCK_SIZE: usize = 32;

/// Q8_0 Block: 32 elements compressed to 8-bit integers + 1 float scale
#[derive(Debug, Clone)]
pub struct BlockQ8_0 {
    pub scale: f32,
    pub qs: [i8; BLOCK_SIZE],
}

/// Q4_0 Block: 32 elements compressed to 4-bit nibbles + 1 float scale (16 bytes total for weights)
#[derive(Debug, Clone)]
pub struct BlockQ4_0 {
    pub scale: f32,
    pub qs: [u8; BLOCK_SIZE / 2],
}

pub struct Quantizer;

impl Quantizer {
    /// Compresses standard f32 arrays into Q8_0 blocks.
    /// Optimized with reciprocal multiplication and zero bounds-checking branches.
    pub fn quantize_q8(data: &[f32]) -> Result<Vec<BlockQ8_0>, RagError> {
        if data.is_empty() || data.len() % BLOCK_SIZE != 0 {
            return Err(RagError::QuantizationError(
                format!("Vector size ({}) must be a multiple of block size (32).", data.len())
            ));
        }

        let mut blocks = Vec::with_capacity(data.len() / BLOCK_SIZE);

        for chunk in data.chunks_exact(BLOCK_SIZE) {
            let mut max = 0.0f32;
            for &val in chunk {
                let abs_val = val.abs();
                if abs_val > max {
                    max = abs_val;
                }
            }

            let scale = max / 127.0;
            let mut qs = [0i8; BLOCK_SIZE];

            if scale > 0.0 {
                // CELERON TUNING: Compute the reciprocal multiplication factor once.
                // This replaces 32 slow divisions with 32 blazing-fast multiplications.
                let inv_scale = 1.0 / scale;
                
                // Using a direct zip iterator tells the compiler both arrays have matching lengths,
                // completely removing all array bounds-checking panics from the assembly loop.
                for (qs_elem, &chunk_val) in qs.iter_mut().zip(chunk) {
                    let v = (chunk_val * inv_scale).round();
                    *qs_elem = v.clamp(-127.0, 127.0) as i8;
                }
            }

            blocks.push(BlockQ8_0 { scale, qs });
        }

        Ok(blocks)
    }

    /// Decompresses Q8_0 blocks back into clean f32 elements using contiguous loops
    pub fn dequantize_q8(blocks: &[BlockQ8_0]) -> Vec<f32> {
        let mut output = Vec::with_capacity(blocks.len() * BLOCK_SIZE);
        for block in blocks {
            let scale = block.scale;
            // Iterating over the array directly bypasses index counters and maximizes cache efficiency
            for &q in &block.qs {
                output.push(q as f32 * scale);
            }
        }
        output
    }

    /// Compresses standard f32 arrays into Q4_0 blocks (Packing two 4-bit nibbles into a single byte)
    pub fn quantize_q4(data: &[f32]) -> Result<Vec<BlockQ4_0>, RagError> {
        if data.is_empty() || data.len() % BLOCK_SIZE != 0 {
            return Err(RagError::QuantizationError(
                format!("Vector size ({}) must be a multiple of block size (32).", data.len())
            ));
        }

        let mut blocks = Vec::with_capacity(data.len() / BLOCK_SIZE);

        for chunk in data.chunks_exact(BLOCK_SIZE) {
            let mut max = 0.0f32;
            for &val in chunk {
                let abs_val = val.abs();
                if abs_val > max {
                    max = abs_val;
                }
            }

            let scale = max / 7.0;
            let mut qs = [0u8; BLOCK_SIZE / 2];

            if scale > 0.0 {
                let inv_scale = 1.0 / scale;

                // chunks_exact(2) guarantees pairs, allowing the compiler to optimize the internal indices
                for (i, pair) in chunk.chunks_exact(2).enumerate() {
                    let v0 = (pair[0] * inv_scale).round().clamp(-7.0, 7.0) as i8;
                    let v1 = (pair[1] * inv_scale).round().clamp(-7.0, 7.0) as i8;
                    
                    let u0 = (v0 + 8) as u8 & 0x0F;
                    let u1 = (v1 + 8) as u8 & 0x0F;

                    qs[i] = u0 | (u1 << 4);
                }
            }

            blocks.push(BlockQ4_0 { scale, qs });
        }

        Ok(blocks)
    }

    /// Decompresses Q4_0 blocks back into clean f32 elements with minimal bit shifting
    pub fn dequantize_q4(blocks: &[BlockQ4_0]) -> Vec<f32> {
        let mut output = Vec::with_capacity(blocks.len() * BLOCK_SIZE);
        for block in blocks {
            let scale = block.scale;
            for &q in &block.qs {
                let u0 = q & 0x0F;
                let u1 = q >> 4; // Masking with 0x0F is redundant after shifting a u8 right by 4 bits

                let v0 = (u0 as i8 - 8) as f32;
                let v1 = (u1 as i8 - 8) as f32;

                output.push(v0 * scale);
                output.push(v1 * scale);
            }
        }
        output
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper to assert two vectors are reasonably close after lossy compression
    fn assert_f32_slices_near(actual: &[f32], expected: &[f32], tolerance: f32) {
        assert_eq!(actual.len(), expected.len(), "Slice length mismatch");
        for (i, (&act, &exp)) in actual.iter().zip(expected).enumerate() {
            assert!(
                (act - exp).abs() <= tolerance,
                "Divergence too high at index {}: actual={}, expected={} (diff={})",
                i, act, exp, (act - exp).abs()
            );
        }
    }

    // ==========================================
    // 1. ALIGNMENT & GUARDRAIL TESTS
    // ==========================================

    #[test]
    fn test_invalid_vector_sizes_throw_errors() {
        // Empty inputs must fail
        assert!(Quantizer::quantize_q8(&[]).is_err());
        assert!(Quantizer::quantize_q4(&[]).is_err());

        // Inputs that aren't multiples of BLOCK_SIZE (32) must fail
        let unaligned_data = vec![1.0f32; 31];
        
        let q8_res = Quantizer::quantize_q8(&unaligned_data);
        assert!(q8_res.is_err());
        match q8_res.unwrap_err() {
            RagError::QuantizationError(msg) => assert!(msg.contains("must be a multiple of block size")),
            _ => panic!("Expected QuantizationError variant"),
        }

        let q4_res = Quantizer::quantize_q4(&unaligned_data);
        assert!(q4_res.is_err());
    }

    #[test]
    fn test_zero_vector_handling_does_not_panic() {
        // An all-zero array should produce a scale of 0.0 without triggering division by zero
        let zero_data = vec![0.0f32; BLOCK_SIZE];
        
        let q8_blocks = Quantizer::quantize_q8(&zero_data).unwrap();
        assert_eq!(q8_blocks[0].scale, 0.0);
        let deq_q8 = Quantizer::dequantize_q8(&q8_blocks);
        assert_eq!(deq_q8, zero_data);

        let q4_blocks = Quantizer::quantize_q4(&zero_data).unwrap();
        assert_eq!(q4_blocks[0].scale, 0.0);
        let deq_q4 = Quantizer::dequantize_q4(&q4_blocks);
        assert_eq!(deq_q4, zero_data);
    }

    // ==========================================
    // 2. Q8_0 COMPRESSION ROUNDTRIP 
    // ==========================================

    #[test]
    fn test_q8_0_precision_roundtrip() {
        // Create a distinct spectrum spanning from -10.0 up to +10.0
        let mut original_data = Vec::with_capacity(BLOCK_SIZE);
        for i in 0..BLOCK_SIZE {
            let val = -10.0 + (i as f32 * 20.0 / (BLOCK_SIZE - 1) as f32);
            original_data.push(val);
        }

        // Compress and extract
        let blocks = Quantizer::quantize_q8(&original_data).unwrap();
        assert_eq!(blocks.len(), 1);
        
        let reconstructed = Quantizer::dequantize_q8(&blocks);

        // Q8_0 quantization splits the maximum range into 127 discrete intervals.
        // The expected precision error should remain tightly bound below (max / 127).
        let max_expected_error = 10.0 / 127.0;
        assert_f32_slices_near(&reconstructed, &original_data, max_expected_error);
    }

    // ==========================================
    // 3. Q4_0 NIBBLE-PACKING ROUNDTRIP
    // ==========================================

    #[test]
    fn test_q4_0_packing_and_shifting() {
        let mut original_data = Vec::with_capacity(BLOCK_SIZE);
        for i in 0..BLOCK_SIZE {
            let val = -3.5 + (i as f32 * 7.0 / (BLOCK_SIZE - 1) as f32);
            original_data.push(val);
        }

        // Compress down to 4-bit blocks
        let blocks = Quantizer::quantize_q4(&original_data).unwrap();
        assert_eq!(blocks.len(), 1);
        
        // Assert that 32 elements are tightly packed down into 16 bytes (u8 array)
        assert_eq!(blocks[0].qs.len(), 16);

        let reconstructed = Quantizer::dequantize_q4(&blocks);

        // Q4_0 splits the maximum range into only 7 intervals (-7 to 7).
        // It's much more lossy, so we widen our tolerance criteria to match (max / 7.0).
        let max_expected_error = 3.5 / 7.0;
        assert_f32_slices_near(&reconstructed, &original_data, max_expected_error);
    }

    // ==========================================
    // 4. CLAMPING & CORNER CASES
    // ==========================================

    #[test]
    fn test_extreme_value_clamping_ranges() {
        let mut data = vec![0.1f32; BLOCK_SIZE];
        // Introduce an extreme outlier that forces a high scale value
        data[0] = 50.0; 
        data[1] = -50.0;

        // Q8 verification
        let q8_blocks = Quantizer::quantize_q8(&data).unwrap();
        // The value 50.0 should hit the ceiling and scale perfectly to 127 or -127
        assert_eq!(q8_blocks[0].qs[0], 127);
        assert_eq!(q8_blocks[0].qs[1], -127);

        // Q4 verification
        let q4_blocks = Quantizer::quantize_q4(&data).unwrap();
        // Index 0 packed into lowest nibble of byte 0: value 7 + offset 8 = 15 (0x0F)
        // Index 1 packed into highest nibble of byte 0: value -7 + offset 8 = 1 (0x01)
        // Expected byte sequence combination: 0x0F | (0x01 << 4) = 0x1F
        assert_eq!(q4_blocks[0].qs[0], 0x1F);
    }
}