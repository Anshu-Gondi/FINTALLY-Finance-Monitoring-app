//! Audio preparation pipeline for Fintally Voice.
//!
//! Pipeline:
//!
//!     compressed source audio
//!             │
//!             ▼
//!       Symphonia decoder
//!             │
//!             ▼
//!       interleaved FP32
//!             │
//!             ▼
//!          mono FP32
//!             │
//!             ▼
//!        Rubato resampler
//!             │
//!             ▼
//!        16 kHz mono FP32
//!             │
//!             ▼
//!       FP32 IEEE float WAV writer
//!
//! This module is offline-builder code only.
//!
//! Runtime audio processing remains in c_core / fintally_voice.

use std::fs::{self, File};
use std::io::{BufWriter, Write};
use std::path::Path;

use anyhow::{bail, Context, Result};
use tracing::{info, warn};

use crate::dataset::{
    DatasetSelection, PreparedManifestRecord, CANONICAL_CHANNELS, CANONICAL_SAMPLE_RATE_HZ,
    PREPARED_MANIFEST_SCHEMA_VERSION,
};

pub mod decoder;
pub mod resampler;
pub mod wav;

// =============================================================================
// PREPARATION CONFIGURATION
// =============================================================================

#[derive(Debug, Clone, Copy)]
pub struct PrepareConfig {
    /// Target output sample rate.
    pub output_sample_rate: u32,

    /// Target number of channels.
    ///
    /// Current Fintally Voice training pipeline is intentionally mono.
    pub output_channels: u16,
}

// =============================================================================
// PREPARATION SUMMARY
// =============================================================================

#[derive(Debug, Default, Clone, Copy)]
pub struct PreparationSummary {
    pub files_written: usize,
    pub files_skipped: usize,
    pub files_failed: usize,
}

// =============================================================================
// BACKEND CHECK
// =============================================================================

/// Verify that the native Rust audio stack is available.
///
/// This intentionally does not invoke an external process such as FFmpeg.
pub fn check_backend() -> Result<()> {
    // Force initialization of Symphonia's registries.
    let _ = symphonia::default::get_probe();
    let _ = symphonia::default::get_codecs();

    info!("native audio backend is available");

    Ok(())
}

// =============================================================================
// DATASET PREPARATION
// =============================================================================

/// Decode, downmix, resample and write the selected training clips.
///
/// The DatasetSelection has already performed the metadata-duration limit,
/// so this stage only transforms the selected clips.
///
/// Canonical offline-training output:
///
///     16000 Hz
///     mono
///     IEEE float32 WAV
///
/// A versioned `manifest.jsonl` is committed after processing and preserves the
/// transcript, source dataset, client group, split and measured audio duration.
pub fn prepare_dataset(
    selection: &DatasetSelection,
    output_dir: &Path,
    config: &PrepareConfig,
) -> Result<PreparationSummary> {
    validate_prepare_config(config)?;

    fs::create_dir_all(output_dir).with_context(|| {
        format!(
            "failed to create audio output directory: {}",
            output_dir.display()
        )
    })?;

    let manifest_path = output_dir.join("manifest.jsonl");
    let temporary_manifest_path = output_dir.join("manifest.jsonl.tmp");
    if temporary_manifest_path.exists() {
        fs::remove_file(&temporary_manifest_path).with_context(|| {
            format!(
                "failed to remove stale manifest temporary file: {}",
                temporary_manifest_path.display()
            )
        })?;
    }
    let manifest_file = File::create(&temporary_manifest_path).with_context(|| {
        format!(
            "failed to create temporary manifest: {}",
            temporary_manifest_path.display()
        )
    })?;
    let mut manifest_writer = BufWriter::new(manifest_file);

    // A single resampler instance is reused for every clip with a given source
    // rate; rebuilding FFT resamplers per utterance would waste substantial time.
    let mut resampler_bank = resampler::ResamplerBank::new(config.output_sample_rate);
    let mut summary = PreparationSummary::default();

    for (language, clips) in selection.iter() {
        let language_dir = output_dir.join(language.as_str());
        fs::create_dir_all(&language_dir).with_context(|| {
            format!(
                "failed to create language output directory: {}",
                language_dir.display()
            )
        })?;

        info!(language = %language, clips = clips.len(), "preparing language audio");

        for clip in clips {
            let Some(source_filename) = clip.audio_path.file_name().and_then(|name| name.to_str())
            else {
                warn!(path = %clip.audio_path.display(), "skipping audio path with invalid filename");
                summary.files_skipped += 1;
                continue;
            };

            let sample_id = stable_sample_id(clip, source_filename);
            let output_filename = format!("{sample_id}_f32.wav");
            let output_path = language_dir.join(&output_filename);

            match prepare_single_clip(clip, &output_path, config, &mut resampler_bank) {
                Ok(sample_count) => {
                    let duration_ms =
                        sample_count.saturating_mul(1000) / u64::from(config.output_sample_rate);
                    let relative_audio_path = format!("{}/{}", language.as_str(), output_filename);
                    let record = PreparedManifestRecord {
                        schema_version: PREPARED_MANIFEST_SCHEMA_VERSION,
                        sample_id: sample_id.clone(),
                        language: language.as_str().to_owned(),
                        dataset: clip.dataset.as_str().to_owned(),
                        audio_path: relative_audio_path,
                        sample_rate_hz: config.output_sample_rate,
                        channels: config.output_channels,
                        sample_format: "ieee-float32".to_owned(),
                        sample_count,
                        duration_ms,
                        source_duration_ms: clip.duration_ms,
                        source_audio_filename: source_filename.to_owned(),
                        client_id: clip.client_id.clone(),
                        source_split: clip.source_split.clone(),
                        split: clip.split.as_str().to_owned(),
                        transcript: clip.transcript.clone(),
                        prompt: clip.prompt.clone(),
                    };

                    serde_json::to_writer(&mut manifest_writer, &record)
                        .context("failed to serialize prepared dataset manifest record")?;
                    manifest_writer
                        .write_all(b"\n")
                        .context("failed to write newline to prepared dataset manifest")?;
                    summary.files_written += 1;
                }
                Err(error) => {
                    summary.files_failed += 1;
                    warn!(
                        language = %language,
                        source = %clip.audio_path.display(),
                        error = %error,
                        "failed to prepare audio clip"
                    );
                    let _ = fs::remove_file(&output_path);
                }
            }
        }
    }

    info!(
        files_written = summary.files_written,
        files_skipped = summary.files_skipped,
        files_failed = summary.files_failed,
        "audio dataset preparation finished"
    );

    if summary.files_written == 0 {
        drop(manifest_writer);
        let _ = fs::remove_file(&temporary_manifest_path);
        bail!("audio preparation produced zero usable FP32 WAV files; refusing to publish an empty manifest");
    }

    manifest_writer
        .flush()
        .context("failed to flush prepared manifest")?;
    manifest_writer
        .get_ref()
        .sync_all()
        .context("failed to sync prepared manifest")?;
    drop(manifest_writer);
    fs::rename(&temporary_manifest_path, &manifest_path).with_context(|| {
        format!(
            "failed to commit prepared manifest {} -> {}",
            temporary_manifest_path.display(),
            manifest_path.display()
        )
    })?;

    info!(
        manifest = %manifest_path.display(),
        files = summary.files_written,
        "prepared FP32 WAV files and transcript manifest committed"
    );

    Ok(summary)
}

fn stable_sample_id(clip: &crate::dataset::TrainingClip, source_filename: &str) -> String {
    let stem = Path::new(source_filename)
        .file_stem()
        .and_then(|value| value.to_str())
        .unwrap_or("audio");
    let mut safe_stem = String::with_capacity(stem.len().min(64));
    for character in stem.chars().take(64) {
        if character.is_ascii_alphanumeric() || character == '-' || character == '_' {
            safe_stem.push(character);
        } else {
            safe_stem.push('_');
        }
    }
    while safe_stem.starts_with('_') {
        safe_stem.remove(0);
    }
    while safe_stem.ends_with('_') {
        safe_stem.pop();
    }
    if safe_stem.is_empty() {
        safe_stem.push_str("audio");
    }

    // Stable FNV-1a ID avoids basename collisions without depending on absolute
    // machine-specific paths or randomized hash seeds.
    let key = format!(
        "{}|{}|{}",
        clip.language.as_str(),
        clip.dataset.as_str(),
        source_filename.to_ascii_lowercase()
    );
    let mut hash = 0xcbf2_9ce4_8422_2325_u64;
    for byte in key.as_bytes() {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    format!("{safe_stem}_{hash:016x}")
}

// =============================================================================
// SINGLE CLIP
// =============================================================================

fn prepare_single_clip(
    clip: &crate::dataset::TrainingClip,
    output_path: &Path,
    config: &PrepareConfig,
    resampler_bank: &mut resampler::ResamplerBank,
) -> Result<u64> {
    // -------------------------------------------------------------------------
    // 1. Decode compressed source → mono FP32.
    // -------------------------------------------------------------------------

    let decoded = decoder::decode_to_mono_f32(&clip.audio_path)
        .with_context(|| format!("failed to decode {}", clip.audio_path.display()))?;

    // -------------------------------------------------------------------------
    // 2. Sanity check decoded data.
    // -------------------------------------------------------------------------

    anyhow::ensure!(
        !decoded.samples.is_empty(),
        "decoder produced zero samples for {}",
        clip.audio_path.display()
    );

    anyhow::ensure!(
        decoded.samples.iter().all(|sample| sample.is_finite()),
        "decoded audio contains non-finite samples: {}",
        clip.audio_path.display()
    );

    // -------------------------------------------------------------------------
    // 3. Resample mono FP32 → target rate.
    // -------------------------------------------------------------------------

    let resampled = resampler_bank
        .resample(decoded.sample_rate, decoded.samples)
        .with_context(|| {
            format!(
                "failed to resample {} Hz → {} Hz for {}",
                decoded.sample_rate,
                config.output_sample_rate,
                clip.audio_path.display()
            )
        })?;

    anyhow::ensure!(
        !resampled.is_empty(),
        "resampler produced zero samples for {}",
        clip.audio_path.display()
    );

    // -------------------------------------------------------------------------
    // 4. Current training representation is mono.
    // -------------------------------------------------------------------------

    if config.output_channels != 1 {
        bail!(
            "only mono output is currently supported; requested {} channels",
            config.output_channels
        );
    }

    // -------------------------------------------------------------------------
    // 5. Write the canonical IEEE float32 WAV. No intermediate PCM16
    // quantization or clipping is performed in the offline-training path.
    // -------------------------------------------------------------------------

    let sample_count =
        u64::try_from(resampled.len()).context("resampled clip length does not fit in u64")?;

    wav::write_f32_mono(output_path, &resampled, config.output_sample_rate)
        .with_context(|| format!("failed to write {}", output_path.display()))?;

    Ok(sample_count)
}

// =============================================================================
// CONFIG VALIDATION
// =============================================================================

fn validate_prepare_config(config: &PrepareConfig) -> Result<()> {
    anyhow::ensure!(
        config.output_sample_rate == CANONICAL_SAMPLE_RATE_HZ,
        "Fintally Voice offline training currently requires the canonical {} Hz sample rate",
        CANONICAL_SAMPLE_RATE_HZ
    );

    anyhow::ensure!(
        config.output_channels == CANONICAL_CHANNELS,
        "Fintally Voice preparation currently requires mono output"
    );

    Ok(())
}
