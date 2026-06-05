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
    /// Compresses standard f32 arrays into Q8_0 blocks
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
                if val.abs() > max {
                    max = val.abs();
                }
            }

            let scale = max / 127.0;
            let mut qs = [0i8; BLOCK_SIZE];

            if scale > 0.0 {
                for i in 0..BLOCK_SIZE {
                    // Scale and clamp safely within signed 8-bit bounds
                    let v = (chunk[i] / scale).round();
                    qs[i] = v.clamp(-127.0, 127.0) as i8;
                }
            }

            blocks.push(BlockQ8_0 { scale, qs });
        }

        Ok(blocks)
    }

    /// Decompresses Q8_0 blocks back into clean f32 elements
    pub fn dequantize_q8(blocks: &[BlockQ8_0]) -> Vec<f32> {
        let mut output = Vec::with_capacity(blocks.len() * BLOCK_SIZE);
        for block in blocks {
            for i in 0..BLOCK_SIZE {
                output.push(block.qs[i] as f32 * block.scale);
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
                if val.abs() > max {
                    max = val.abs();
                }
            }

            let scale = max / 7.0; // 4-bit signed ranges max out at 7
            let mut qs = [0u8; BLOCK_SIZE / 2];

            if scale > 0.0 {
                for i in 0..(BLOCK_SIZE / 2) {
                    let v0 = (chunk[i * 2] / scale).round().clamp(-7.0, 7.0) as i8;
                    let v1 = (chunk[i * 2 + 1] / scale).round().clamp(-7.0, 7.0) as i8;
                    
                    // Map signed [-7, 7] to unsigned 4-bit space safely
                    let u0 = (v0 + 8) as u8 & 0x0F;
                    let u1 = (v1 + 8) as u8 & 0x0F;

                    qs[i] = u0 | (u1 << 4);
                }
            }

            blocks.push(BlockQ4_0 { scale, qs });
        }

        Ok(blocks)
    }

    /// Decompresses Q4_0 blocks back into clear f32 elements
    pub fn dequantize_q4(blocks: &[BlockQ4_0]) -> Vec<f32> {
        let mut output = Vec::with_capacity(blocks.len() * BLOCK_SIZE);
        for block in blocks {
            for i in 0..(BLOCK_SIZE / 2) {
                let u0 = block.qs[i] & 0x0F;
                let u1 = (block.qs[i] >> 4) & 0x0F;

                let v0 = (u0 as i8 - 8) as f32;
                let v1 = (u1 as i8 - 8) as f32;

                output.push(v0 * block.scale);
                output.push(v1 * block.scale);
            }
        }
        output
    }
}