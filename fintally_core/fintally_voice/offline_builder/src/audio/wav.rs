//! WAV I/O for Fintally Voice offline preparation.
//!
//! The training representation is IEEE float32 WAV, keeping the decoded and
//! resampled FP32 signal intact across the preparation boundary. PCM16 remains
//! available for runtime-compatibility artifacts, but is not used as the
//! canonical offline-training intermediate.

use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use hound::{SampleFormat, WavSpec, WavWriter};

// =============================================================================
// WAV SPEC
// =============================================================================

fn pcm16_mono_spec(sample_rate: u32) -> WavSpec {
    WavSpec {
        channels: 1,
        sample_rate,
        bits_per_sample: 16,
        sample_format: SampleFormat::Int,
    }
}

// =============================================================================
// PUBLIC API
// =============================================================================

/// Write mono FP32 samples as a PCM16 WAV file.
///
/// Samples are expected to be normalized approximately to [-1.0, 1.0].
///
/// Values outside that range are safely clamped at the final PCM boundary.
pub fn write_pcm16_mono(path: &Path, samples: &[f32], sample_rate: u32) -> Result<()> {
    anyhow::ensure!(sample_rate > 0, "WAV sample rate must be greater than zero");

    anyhow::ensure!(!samples.is_empty(), "cannot write an empty WAV file");

    anyhow::ensure!(
        samples.iter().all(|sample| sample.is_finite()),
        "cannot write non-finite audio samples"
    );

    // Hound's optimized i16 writer takes a u32 sample count.
    let sample_count = u32::try_from(samples.len()).context(
        "audio clip contains more than \
                 u32::MAX samples",
    )?;

    let spec = pcm16_mono_spec(sample_rate);

    let mut writer = WavWriter::create(path, spec)
        .with_context(|| format!("failed to create WAV: {}", path.display()))?;

    {
        let mut sample_writer = writer.get_i16_writer(sample_count);

        for &sample in samples {
            sample_writer.write_sample(fp32_to_pcm16(sample));
        }

        sample_writer
            .flush()
            .with_context(|| format!("failed to flush WAV samples: {}", path.display()))?;
    }

    writer
        .finalize()
        .with_context(|| format!("failed to finalize WAV: {}", path.display()))?;

    Ok(())
}

// =============================================================================
// FP32 WAV
// =============================================================================

fn f32_mono_spec(sample_rate: u32) -> WavSpec {
    WavSpec {
        channels: 1,
        sample_rate,
        bits_per_sample: 32,
        sample_format: SampleFormat::Float,
    }
}

/// Write mono FP32 samples to an IEEE float32 WAV without PCM quantization.
///
/// Samples are checked for finiteness but are not clipped to [-1, 1]. Floating
/// point WAV can represent finite overshoots from filtering/resampling; silently
/// clipping them here would alter the signal before acoustic feature extraction.
/// The complete file is first written to a sibling temporary file and renamed
/// into place only after Hound finalizes the WAV header.
pub fn write_f32_mono(path: &Path, samples: &[f32], sample_rate: u32) -> Result<()> {
    anyhow::ensure!(sample_rate > 0, "WAV sample rate must be greater than zero");
    anyhow::ensure!(!samples.is_empty(), "cannot write an empty WAV file");
    anyhow::ensure!(
        samples.iter().all(|sample| sample.is_finite()),
        "cannot write non-finite audio samples"
    );
    u32::try_from(samples.len()).context("audio clip contains more than u32::MAX samples")?;

    let temporary_path = temporary_sibling(path);
    if temporary_path.exists() {
        fs::remove_file(&temporary_path).with_context(|| {
            format!(
                "failed to remove stale temporary WAV: {}",
                temporary_path.display()
            )
        })?;
    }

    let result = (|| -> Result<()> {
        let mut writer = WavWriter::create(&temporary_path, f32_mono_spec(sample_rate))
            .with_context(|| {
                format!(
                    "failed to create temporary FP32 WAV: {}",
                    temporary_path.display()
                )
            })?;
        for &sample in samples {
            writer.write_sample(sample).with_context(|| {
                format!(
                    "failed to write FP32 sample to {}",
                    temporary_path.display()
                )
            })?;
        }
        writer.finalize().with_context(|| {
            format!("failed to finalize FP32 WAV: {}", temporary_path.display())
        })?;
        fs::rename(&temporary_path, path).with_context(|| {
            format!(
                "failed to commit FP32 WAV {} -> {}",
                temporary_path.display(),
                path.display()
            )
        })?;
        Ok(())
    })();

    if result.is_err() {
        let _ = fs::remove_file(&temporary_path);
    }
    result
}

/// Validate a prepared IEEE float32, mono WAV and return sample count/duration.
pub fn validate_f32_mono(path: &Path, expected_sample_rate: u32) -> Result<(u64, u64)> {
    let reader = hound::WavReader::open(path)
        .with_context(|| format!("failed to open FP32 WAV for validation: {}", path.display()))?;
    let spec = reader.spec();

    anyhow::ensure!(
        spec.channels == 1,
        "{}: expected mono WAV, got {} channels",
        path.display(),
        spec.channels
    );
    anyhow::ensure!(
        spec.sample_rate == expected_sample_rate,
        "{}: expected {} Hz, got {} Hz",
        path.display(),
        expected_sample_rate,
        spec.sample_rate
    );
    anyhow::ensure!(
        spec.bits_per_sample == 32,
        "{}: expected 32-bit WAV, got {} bits",
        path.display(),
        spec.bits_per_sample
    );
    anyhow::ensure!(
        spec.sample_format == SampleFormat::Float,
        "{}: expected IEEE float WAV",
        path.display()
    );

    let sample_count = u64::from(reader.duration());
    anyhow::ensure!(
        sample_count > 0,
        "{}: WAV contains no samples",
        path.display()
    );
    let duration_ms = sample_count.saturating_mul(1000) / u64::from(spec.sample_rate);
    Ok((sample_count, duration_ms))
}

fn temporary_sibling(path: &Path) -> PathBuf {
    let filename = path
        .file_name()
        .and_then(|value| value.to_str())
        .unwrap_or("prepared.wav");
    path.with_file_name(format!(".{filename}.tmp"))
}

// =============================================================================
// FP32 → PCM16
// =============================================================================

#[inline]
fn fp32_to_pcm16(sample: f32) -> i16 {
    // Protect the conversion boundary.
    let sample = sample.clamp(-1.0, 1.0);

    // Use the full signed 16-bit range:
    //
    //     -1.0 → -32768
    //      1.0 →  32767
    //
    // Keeping the conversion here, instead of earlier in the DSP pipeline,
    // preserves FP32 precision through decoding and resampling.
    let scaled = sample * 32_768.0;

    scaled.round().clamp(i16::MIN as f32, i16::MAX as f32) as i16
}

// =============================================================================
// VALIDATION HELPER
// =============================================================================

/// Validate that a generated WAV has the canonical training format.
pub fn validate_pcm16_mono(path: &Path, expected_sample_rate: u32) -> Result<u64> {
    let reader = hound::WavReader::open(path)
        .with_context(|| format!("failed to open WAV for validation: {}", path.display()))?;

    let spec = reader.spec();

    anyhow::ensure!(
        spec.channels == 1,
        "{}: expected mono WAV, got {} channels",
        path.display(),
        spec.channels
    );

    anyhow::ensure!(
        spec.sample_rate == expected_sample_rate,
        "{}: expected {} Hz, got {} Hz",
        path.display(),
        expected_sample_rate,
        spec.sample_rate
    );

    anyhow::ensure!(
        spec.bits_per_sample == 16,
        "{}: expected 16-bit WAV, got {} bits",
        path.display(),
        spec.bits_per_sample
    );

    anyhow::ensure!(
        spec.sample_format == SampleFormat::Int,
        "{}: expected integer PCM WAV",
        path.display()
    );

    let samples = u64::from(reader.duration());

    let duration_ms = samples.saturating_mul(1000) / u64::from(expected_sample_rate);

    Ok(duration_ms)
}

// =============================================================================
// TESTS
// =============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pcm16_clamps_positive_peak() {
        assert_eq!(fp32_to_pcm16(1.0), i16::MAX);
    }

    #[test]
    fn pcm16_clamps_negative_peak() {
        assert_eq!(fp32_to_pcm16(-1.0), i16::MIN);
    }

    #[test]
    fn pcm16_zero_is_zero() {
        assert_eq!(fp32_to_pcm16(0.0), 0);
    }
}

#[cfg(test)]
mod fp32_tests {
    use super::*;

    #[test]
    fn f32_wav_round_trips_samples_without_pcm16_quantization() {
        let directory =
            std::env::temp_dir().join(format!("fintally_voice_wav_test_{}", std::process::id()));
        std::fs::create_dir_all(&directory).expect("create test directory");
        let path = directory.join("tiny_f32.wav");
        let mut input: Vec<f32> = (0..16_000)
            .map(|index| ((index as f32) * 0.003_17).sin() * 0.987_654_3)
            .collect();
        input[0] = 0.0;
        input[1] = 0.123_456_7;
        input[2] = -0.876_543_2;
        input[3] = 1.01; // Float WAV must not silently clip finite overshoots.

        write_f32_mono(&path, &input, 16_000).expect("write float WAV");
        let reader = hound::WavReader::open(&path).expect("read float WAV");
        assert_eq!(reader.spec().sample_format, SampleFormat::Float);
        assert_eq!(reader.spec().bits_per_sample, 32);
        let output: Vec<f32> = reader
            .into_samples::<f32>()
            .map(|sample| sample.expect("valid FP32 sample"))
            .collect();
        assert_eq!(input.as_slice(), output.as_slice());
        assert_eq!(output[3], 1.01);

        let (count, duration_ms) = validate_f32_mono(&path, 16_000).expect("validate float WAV");
        assert_eq!(count, input.len() as u64);
        assert_eq!(duration_ms, 1_000);
        std::fs::remove_dir_all(directory).expect("remove test directory");
    }
}
