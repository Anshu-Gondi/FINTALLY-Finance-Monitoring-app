//! High-quality fixed-rate audio resampling.
//!
//! Current canonical target:
//!
//!     source_rate → 16000 Hz
//!
//! Input:
//!
//!     mono FP32
//!
//! Output:
//!
//!     mono FP32
//!
//! The resampler is intentionally kept independent from WAV I/O.

use std::collections::hash_map::Entry;
use std::collections::HashMap;

use anyhow::{anyhow, Context, Result};

use audioadapter_buffers::owned::InterleavedOwned;
use rubato::{Fft, FixedSync, Resampler};

// =============================================================================
// CONFIGURATION
// =============================================================================

const RESAMPLER_CHUNK_SIZE: usize = 1024;

// =============================================================================
// RESAMPLER BANK
// =============================================================================

/// Reusable resamplers keyed by source sample rate.
///
/// Example:
///
///     32000 → 16000
///     44100 → 16000
///     48000 → 16000
///
/// If a dataset uses the same source rate repeatedly, the expensive resampler
/// construction is performed only once for that source rate.
pub struct ResamplerBank {
    target_sample_rate: u32,

    resamplers: HashMap<u32, Fft<f32>>,
}

impl ResamplerBank {
    pub fn new(target_sample_rate: u32) -> Self {
        Self {
            target_sample_rate,
            resamplers: HashMap::new(),
        }
    }

    /// Resample a mono FP32 clip.
    ///
    /// When the source rate already equals the target rate, the original
    /// allocation is returned without constructing a resampler.
    pub fn resample(&mut self, input_sample_rate: u32, samples: Vec<f32>) -> Result<Vec<f32>> {
        anyhow::ensure!(
            input_sample_rate > 0,
            "input sample rate must be greater than zero"
        );

        anyhow::ensure!(
            self.target_sample_rate > 0,
            "target sample rate must be greater than zero"
        );

        anyhow::ensure!(!samples.is_empty(), "cannot resample an empty audio buffer");

        if input_sample_rate == self.target_sample_rate {
            return Ok(samples);
        }

        anyhow::ensure!(
            samples.iter().all(|sample| sample.is_finite()),
            "input audio contains non-finite samples"
        );

        let input_frames = samples.len();

        // InterleavedOwned owns the Vec directly, avoiding a second input copy.
        let input = InterleavedOwned::<f32>::new_from(samples, 1, input_frames)
            .map_err(|error| anyhow!("failed to construct resampler input buffer: {error}"))?;

        // ---------------------------------------------------------------------
        // Get or create the resampler for this exact source rate.
        // ---------------------------------------------------------------------

        let resampler = match self.resamplers.entry(input_sample_rate) {
            Entry::Occupied(entry) => entry.into_mut(),

            Entry::Vacant(entry) => {
                let resampler = Fft::<f32>::new(
                    input_sample_rate as usize,
                    self.target_sample_rate as usize,
                    RESAMPLER_CHUNK_SIZE,
                    1,
                    FixedSync::Both,
                )
                .with_context(|| {
                    format!(
                        "failed to create {} Hz → {} Hz FFT resampler",
                        input_sample_rate, self.target_sample_rate
                    )
                })?;

                entry.insert(resampler)
            }
        };

        // Rubato's process_all() is specifically intended for complete clips.
        // It processes internally in chunks and trims the filter startup delay.
        let output = resampler
            .process_all(&input, input_frames, None)
            .map_err(|error| {
                anyhow!(
                    "resampling failed for {} Hz → {} Hz: {}",
                    input_sample_rate,
                    self.target_sample_rate,
                    error
                )
            })?;

        let result = output.take_data();

        anyhow::ensure!(!result.is_empty(), "resampler produced zero samples");

        anyhow::ensure!(
            result.iter().all(|sample| sample.is_finite()),
            "resampler produced non-finite samples"
        );

        Ok(result)
    }
}

// =============================================================================
// SIMPLE ONE-SHOT API
// =============================================================================

/// Convenience function for callers that only need one conversion.
///
/// The main dataset pipeline should use `ResamplerBank` so resamplers can be
/// reused across thousands of clips.
pub fn resample_mono_f32(
    samples: Vec<f32>,
    input_sample_rate: u32,
    output_sample_rate: u32,
) -> Result<Vec<f32>> {
    let mut bank = ResamplerBank::new(output_sample_rate);

    bank.resample(input_sample_rate, samples)
}

// =============================================================================
// TESTS
// ==============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn identical_rate_returns_input() {
        let samples = vec![0.0_f32, 0.25, -0.5, 1.0];

        let mut bank = ResamplerBank::new(16_000);

        let result = bank.resample(16_000, samples.clone()).unwrap();

        assert_eq!(result, samples);
    }

    #[test]
    fn downsample_produces_output() {
        let samples = (0..16_000)
            .map(|index| (index as f32 * 0.001).sin())
            .collect::<Vec<_>>();

        let mut bank = ResamplerBank::new(16_000);

        let result = bank.resample(32_000, samples).unwrap();

        assert!(!result.is_empty());

        // Approximately one half after 32k -> 16k.
        assert!(result.len() > 15_000 && result.len() < 17_000);

        assert!(result.iter().all(|sample| sample.is_finite()));
    }
}
