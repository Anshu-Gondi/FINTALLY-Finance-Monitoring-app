//! Native FP32 feature extraction and training-only normalization for FinTally Voice.
//!
//! The prepared-data contract is mono, 16 kHz, IEEE float32 WAV. This module
//! reads that format, creates overlapping 320-sample frames at a 160-sample hop,
//! and sends each frame to the existing native C23 DSP bridge. Each frame yields
//! the canonical 32-dimensional FP32 acoustic vector.
//!
//! Normalization statistics are fitted only from the `train` split and then
//! applied unchanged to train, validation, and test. This keeps held-out samples
//! out of the fitted preprocessing state.
//!
//! Feature frames remain grouped by clip so transcript/split metadata stays
//! aligned with its audio and the builder does not need to flatten and duplicate
//! a potentially multi-gigabyte feature matrix during extraction.

use std::path::Path;

use anyhow::{anyhow, ensure, Context, Result};
use hound::{SampleFormat, WavReader};
use rayon::prelude::*;
use tracing::{info, warn};

use crate::dataset::{
    DataSplit, DatasetKind, LanguageId, PreparedClip, PreparedDataset, CANONICAL_FEATURE_DIMENSION,
    CANONICAL_SAMPLE_RATE_HZ,
};

mod ffi;

pub const FRAME_SAMPLES: usize = ffi::FRAME_SAMPLES;
pub const FEATURE_DIMENSION: usize = ffi::FEATURE_DIMENSION;

/// Canonical hop between frame starts: 10 ms at 16 kHz.
///
/// The native public DSP contract defines a 320-sample frame and 160-sample hop.
/// The narrow bridge currently exports the frame size and feature dimension;
/// keep this value synchronized with `FIN_VOICE_HOP_SAMPLES` in `acoustic_dsp.h`.
pub const HOP_SAMPLES: usize = 160;
const RESERVED_FEATURE_INDEX: usize = 31;
const NORMALIZATION_SCHEMA_VERSION: u32 = 1;

/// Immutable configuration for native acoustic feature extraction.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FeatureConfig {
    pub sample_rate: u32,
    pub feature_dim: usize,
}

/// Canonical fixed-size FP32 vector produced by the native DSP ABI.
pub type FeatureVector = [f32; FEATURE_DIMENSION];

/// Provenance and features for one prepared audio clip.
///
/// `frames` owns the extracted features for this clip. Transcript text is copied
/// verbatim from the prepared manifest; text cleanup belongs to a later stage.
#[derive(Debug, Clone)]
pub struct FeatureClip {
    pub sample_id: String,
    pub language: LanguageId,
    pub dataset: DatasetKind,
    pub split: DataSplit,
    pub duration_ms: u64,
    pub source_duration_ms: u64,
    pub client_id: Option<String>,
    pub source_split: Option<String>,
    pub transcript: Option<String>,
    pub prompt: Option<String>,
    pub frames: Vec<FeatureVector>,
}

/// Feature frames grouped by clip, with metadata kept aligned to the audio.
#[derive(Debug, Clone)]
pub struct FeatureSets {
    pub sample_rate: u32,
    pub feature_dim: usize,
    pub clips: Vec<FeatureClip>,
    /// `None` before normalization; `Some` after training-split statistics have
    /// been fitted and applied to every frame.
    pub normalization: Option<FeatureNormalization>,
}

/// Z-score normalization state suitable for recording beside an exported model.
///
/// For each dimension: `normalized = (value - mean) * inv_std`.
/// Constant dimensions use an inverse standard deviation of 1.0; subtracting
/// their mean therefore maps their training values to zero without dividing by
/// an epsilon and amplifying numerical noise.
#[derive(Debug, Clone, PartialEq)]
pub struct FeatureNormalization {
    pub schema_version: u32,
    pub mean: FeatureVector,
    pub inv_std: FeatureVector,
    pub training_frame_count: u64,
}

impl FeatureSets {
    pub fn total_frames(&self) -> usize {
        self.clips.iter().map(|clip| clip.frames.len()).sum()
    }

    pub fn total_clips(&self) -> usize {
        self.clips.len()
    }

    pub fn frame_count_for_split(&self, split: DataSplit) -> usize {
        self.clips
            .iter()
            .filter(|clip| clip.split == split)
            .map(|clip| clip.frames.len())
            .sum()
    }

    pub fn clips_for_split(&self, split: DataSplit) -> impl Iterator<Item = &FeatureClip> {
        self.clips.iter().filter(move |clip| clip.split == split)
    }

    /// Iterates feature vectors from one split without cloning samples.
    pub fn iter_frames_for_split(&self, split: DataSplit) -> impl Iterator<Item = &FeatureVector> {
        self.clips_for_split(split)
            .flat_map(|clip| clip.frames.iter())
    }

    /// Borrows feature frames without copying or allocating.
    ///
    /// The returned slice is tied to the lifetime of `clip`, because the
    /// frames are stored in that clip's owned vector.
    #[inline]
    pub fn frames_for_clip<'a>(&self, clip: &'a FeatureClip) -> &'a [FeatureVector] {
        clip.frames.as_slice()
    }
}

/// Extract FP32 features for every prepared clip in deterministic dataset order.
///
/// `workers == 0` uses Rayon’s default pool size. A positive value creates a
/// private pool with exactly that many threads. Native DSP contexts are created
/// by Rayon jobs and reused for multiple clips handled by each job; a context is
/// never shared concurrently.
pub fn extract_dataset_features(
    dataset: &PreparedDataset,
    config: &FeatureConfig,
    workers: usize,
) -> Result<FeatureSets> {
    validate_feature_config(config)?;

    ensure!(
        dataset.total_clips() > 0,
        "prepared dataset contains no clips to extract"
    );

    let mut clip_refs: Vec<&PreparedClip> =
        dataset.iter().flat_map(|(_, clips)| clips.iter()).collect();
    clip_refs.sort_by(|left, right| {
        left.language
            .cmp(&right.language)
            .then_with(|| left.sample_id.cmp(&right.sample_id))
    });

    let extract = || {
        clip_refs
            .par_iter()
            .map_init(
                || ffi::Dsp::new(),
                |dsp_state, clip| {
                    let dsp = match dsp_state {
                        Ok(dsp) => dsp,
                        Err(error) => {
                            return Err(anyhow!(
                                "failed to initialize native DSP context: {error}"
                            ));
                        }
                    };
                    extract_clip_features(*clip, config, dsp)
                },
            )
            .collect::<Vec<Result<FeatureClip>>>()
    };

    // Collect per-item Results first. This means failures can be reported in
    // deterministic manifest order rather than whichever worker fails first.
    let extraction_results = if workers == 0 {
        extract()
    } else {
        let pool = rayon::ThreadPoolBuilder::new()
            .num_threads(workers)
            .thread_name(|index| format!("fintally-feature-{index}"))
            .build()
            .with_context(|| format!("failed to create {workers}-thread feature pool"))?;
        pool.install(extract)
    };

    let clips: Vec<FeatureClip> = extraction_results.into_iter().collect::<Result<Vec<_>>>()?;

    let result = FeatureSets {
        sample_rate: config.sample_rate,
        feature_dim: config.feature_dim,
        clips,
        normalization: None,
    };

    ensure!(
        result.total_frames() > 0,
        "feature extraction produced zero frames"
    );

    info!(
        clips = result.total_clips(),
        frames = result.total_frames(),
        train_frames = result.frame_count_for_split(DataSplit::Train),
        validation_frames = result.frame_count_for_split(DataSplit::Validation),
        test_frames = result.frame_count_for_split(DataSplit::Test),
        languages = dataset.languages(),
        feature_dimension = result.feature_dim,
        sample_rate_hz = result.sample_rate,
        "native FP32 feature extraction completed"
    );

    if result.frame_count_for_split(DataSplit::Validation) == 0 {
        warn!("no validation frames are present; downstream quality metrics will be limited");
    }
    if result.frame_count_for_split(DataSplit::Test) == 0 {
        warn!("no test frames are present; do not treat training metrics as held-out evaluation");
    }

    Ok(result)
}

/// Fit per-dimension normalization on training frames only, then normalize all
/// splits in place. This consumes `feature_sets` so it does not create a second
/// full copy of the feature arrays.
pub fn normalize_feature_sets(mut feature_sets: FeatureSets) -> Result<FeatureSets> {
    validate_feature_sets(&feature_sets)?;
    ensure!(
        feature_sets.normalization.is_none(),
        "feature sets have already been normalized; refusing to apply normalization twice"
    );

    let mut mean = [0.0_f64; FEATURE_DIMENSION];
    let mut m2 = [0.0_f64; FEATURE_DIMENSION];
    let mut count = 0_u64;

    // Welford’s online algorithm gives stable population variance in FP64 even
    // though the stored acoustic features and resulting normalization state are FP32.
    for frame in feature_sets.iter_frames_for_split(DataSplit::Train) {
        count = count
            .checked_add(1)
            .context("training frame count overflow during normalization")?;
        let count_f64 = count as f64;

        for dimension in 0..FEATURE_DIMENSION {
            let value = f64::from(frame[dimension]);
            let delta = value - mean[dimension];
            mean[dimension] += delta / count_f64;
            let delta_after_update = value - mean[dimension];
            m2[dimension] += delta * delta_after_update;
        }
    }

    ensure!(
        count > 0,
        "cannot fit feature normalization: no training frames are present"
    );

    let mut mean_f32 = [0.0_f32; FEATURE_DIMENSION];
    let mut inv_std = [1.0_f32; FEATURE_DIMENSION];

    for dimension in 0..FEATURE_DIMENSION {
        let variance = (m2[dimension] / count as f64).max(0.0);
        let std_dev = variance.sqrt();
        ensure!(
            mean[dimension].is_finite() && std_dev.is_finite(),
            "non-finite normalization statistics in feature dimension {dimension}"
        );

        mean_f32[dimension] = mean[dimension] as f32;
        ensure!(
            mean_f32[dimension].is_finite(),
            "normalization mean is not representable as FP32 in dimension {dimension}"
        );

        // A truly constant/near-constant dimension is centered but not scaled.
        // This is particularly important for the currently reserved zero feature.
        inv_std[dimension] = if std_dev <= 1.0e-12 {
            1.0
        } else {
            (1.0 / std_dev) as f32
        };
        ensure!(
            inv_std[dimension].is_finite(),
            "normalization inverse standard deviation is not representable as FP32 in dimension {dimension}"
        );
    }

    for clip in &mut feature_sets.clips {
        for frame in &mut clip.frames {
            for dimension in 0..FEATURE_DIMENSION {
                let normalized = (f64::from(frame[dimension]) - f64::from(mean_f32[dimension]))
                    * f64::from(inv_std[dimension]);
                let normalized = normalized as f32;
                ensure!(
                    normalized.is_finite(),
                    "normalization produced a non-finite value for sample {:?}, dimension {}",
                    clip.sample_id,
                    dimension
                );
                frame[dimension] = normalized;
            }
        }
    }

    feature_sets.normalization = Some(FeatureNormalization {
        schema_version: NORMALIZATION_SCHEMA_VERSION,
        mean: mean_f32,
        inv_std,
        training_frame_count: count,
    });

    info!(
        training_frames = count,
        total_frames = feature_sets.total_frames(),
        "feature normalization fitted on training split and applied to all splits"
    );

    Ok(feature_sets)
}

fn validate_feature_config(config: &FeatureConfig) -> Result<()> {
    ensure!(
        config.sample_rate == CANONICAL_SAMPLE_RATE_HZ,
        "feature extraction requires {} Hz audio, got {} Hz",
        CANONICAL_SAMPLE_RATE_HZ,
        config.sample_rate
    );
    ensure!(
        config.feature_dim == FEATURE_DIMENSION,
        "native DSP produces {} features; configured feature dimension is {}",
        FEATURE_DIMENSION,
        config.feature_dim
    );
    ensure!(
        FEATURE_DIMENSION == CANONICAL_FEATURE_DIMENSION,
        "native FFI feature dimension ({}) disagrees with dataset contract ({})",
        FEATURE_DIMENSION,
        CANONICAL_FEATURE_DIMENSION
    );
    ensure!(
        FRAME_SAMPLES == 320,
        "native DSP frame contract changed: expected 320 samples, got {FRAME_SAMPLES}"
    );
    ensure!(
        HOP_SAMPLES == 160,
        "native DSP hop contract changed: expected 160 samples, got {HOP_SAMPLES}"
    );
    Ok(())
}

fn extract_clip_features(
    clip: &PreparedClip,
    config: &FeatureConfig,
    dsp: &mut ffi::Dsp,
) -> Result<FeatureClip> {
    let samples = read_prepared_f32_wav(&clip.audio_path, config.sample_rate, clip.sample_count)
        .with_context(|| {
            format!(
                "failed to read prepared audio for sample {:?}",
                clip.sample_id
            )
        })?;

    // Emit a frame every 10 ms. A final partial frame is zero-padded, so clips
    // retain the last hop instead of silently dropping the audio tail.
    let frame_capacity = 1 + (samples.len() - 1) / HOP_SAMPLES;
    let mut frames = Vec::with_capacity(frame_capacity);
    let mut start = 0_usize;

    while start < samples.len() {
        let mut frame = [0.0_f32; FRAME_SAMPLES];
        let available = (samples.len() - start).min(FRAME_SAMPLES);
        frame[..available].copy_from_slice(&samples[start..start + available]);

        let features = dsp.extract_f32(&frame).with_context(|| {
            format!("native feature extraction failed at sample offset {start}")
        })?;

        ensure!(
            features.iter().all(|value| value.is_finite()),
            "native DSP returned non-finite features for sample {:?}, frame offset {start}",
            clip.sample_id
        );
        ensure!(
            features[RESERVED_FEATURE_INDEX] == 0.0,
            "native DSP reserved feature {} must remain zero for sample {:?}",
            RESERVED_FEATURE_INDEX,
            clip.sample_id
        );

        frames.push(features);
        start = start
            .checked_add(HOP_SAMPLES)
            .context("frame offset overflow while extracting features")?;
    }

    ensure!(
        !frames.is_empty(),
        "sample {:?} produced no feature frames",
        clip.sample_id
    );

    Ok(FeatureClip {
        sample_id: clip.sample_id.clone(),
        language: clip.language,
        dataset: clip.dataset,
        split: clip.split,
        duration_ms: clip.duration_ms,
        source_duration_ms: clip.source_duration_ms,
        client_id: clip.client_id.clone(),
        source_split: clip.source_split.clone(),
        transcript: clip.transcript.clone(),
        prompt: clip.prompt.clone(),
        frames,
    })
}

fn read_prepared_f32_wav(
    path: &Path,
    expected_sample_rate: u32,
    expected_sample_count: u64,
) -> Result<Vec<f32>> {
    let mut reader = WavReader::open(path)
        .with_context(|| format!("failed to open prepared WAV: {}", path.display()))?;
    let spec = reader.spec();

    ensure!(
        spec.sample_rate == expected_sample_rate,
        "{}: expected {} Hz, got {} Hz",
        path.display(),
        expected_sample_rate,
        spec.sample_rate
    );
    ensure!(
        spec.channels == 1,
        "{}: expected mono WAV, got {} channels",
        path.display(),
        spec.channels
    );
    ensure!(
        spec.bits_per_sample == 32,
        "{}: expected 32-bit WAV, got {} bits",
        path.display(),
        spec.bits_per_sample
    );
    ensure!(
        spec.sample_format == SampleFormat::Float,
        "{}: expected IEEE float WAV",
        path.display()
    );
    ensure!(
        reader.duration() as u64 == expected_sample_count,
        "{}: manifest says {} samples but WAV header says {}",
        path.display(),
        expected_sample_count,
        reader.duration()
    );

    let capacity = usize::try_from(expected_sample_count)
        .context("prepared audio sample count does not fit this platform's address space")?;
    let mut samples = Vec::with_capacity(capacity);

    for (index, sample_result) in reader.samples::<f32>().enumerate() {
        let sample = sample_result.with_context(|| {
            format!(
                "failed to decode FP32 WAV sample {index} from {}",
                path.display()
            )
        })?;
        ensure!(
            sample.is_finite(),
            "{}: non-finite FP32 audio sample at index {index}",
            path.display()
        );
        samples.push(sample);
    }

    ensure!(
        samples.len() == capacity,
        "{}: decoded {} samples but expected {}",
        path.display(),
        samples.len(),
        capacity
    );
    ensure!(
        !samples.is_empty(),
        "{}: prepared WAV is empty",
        path.display()
    );
    Ok(samples)
}

fn validate_feature_sets(feature_sets: &FeatureSets) -> Result<()> {
    ensure!(
        feature_sets.sample_rate == CANONICAL_SAMPLE_RATE_HZ,
        "feature set sample rate must be {} Hz",
        CANONICAL_SAMPLE_RATE_HZ
    );
    ensure!(
        feature_sets.feature_dim == FEATURE_DIMENSION,
        "feature set dimension must be {}",
        FEATURE_DIMENSION
    );
    ensure!(
        !feature_sets.clips.is_empty(),
        "feature sets contain no clips"
    );

    let mut previous_key: Option<(LanguageId, &str)> = None;
    for clip in &feature_sets.clips {
        ensure!(
            !clip.sample_id.trim().is_empty(),
            "feature clip has an empty sample ID"
        );
        ensure!(
            !clip.frames.is_empty(),
            "feature clip {:?} has no frames",
            clip.sample_id
        );
        if let Some((previous_language, previous_id)) = previous_key {
            ensure!(
                previous_language < clip.language
                    || (previous_language == clip.language
                        && previous_id < clip.sample_id.as_str()),
                "feature clips are not in strict deterministic language/sample-ID order at {:?}",
                clip.sample_id
            );
        }
        previous_key = Some((clip.language, clip.sample_id.as_str()));
        for (frame_index, frame) in clip.frames.iter().enumerate() {
            ensure!(
                frame.iter().all(|value| value.is_finite()),
                "feature clip {:?} frame {} contains a non-finite value",
                clip.sample_id,
                frame_index
            );
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_frame(value: f32) -> FeatureVector {
        let mut frame = [0.0_f32; FEATURE_DIMENSION];
        frame[0] = value;
        frame
    }

    fn test_clip(sample_id: &str, split: DataSplit, values: &[f32]) -> FeatureClip {
        FeatureClip {
            sample_id: sample_id.to_owned(),
            language: LanguageId::En,
            dataset: DatasetKind::Sps,
            split,
            duration_ms: values.len() as u64 * 10,
            source_duration_ms: values.len() as u64 * 10,
            client_id: None,
            source_split: None,
            transcript: Some("test transcript".to_owned()),
            prompt: None,
            frames: values.iter().copied().map(test_frame).collect(),
        }
    }

    fn feature_sets(clips: Vec<FeatureClip>) -> FeatureSets {
        FeatureSets {
            sample_rate: CANONICAL_SAMPLE_RATE_HZ,
            feature_dim: FEATURE_DIMENSION,
            clips,
            normalization: None,
        }
    }

    #[test]
    fn normalization_fits_train_only_and_applies_to_held_out_frames() {
        let input = feature_sets(vec![
            test_clip("a-train", DataSplit::Train, &[1.0, 3.0]),
            test_clip("b-validation", DataSplit::Validation, &[101.0]),
            test_clip("c-test", DataSplit::Test, &[5.0]),
        ]);

        let normalized = normalize_feature_sets(input).expect("normalization should succeed");
        let stats = normalized
            .normalization
            .as_ref()
            .expect("normalization stats");

        assert_eq!(stats.training_frame_count, 2);
        assert!((stats.mean[0] - 2.0).abs() < 1.0e-6);
        assert!((normalized.clips[0].frames[0][0] - -1.0).abs() < 1.0e-5);
        assert!((normalized.clips[0].frames[1][0] - 1.0).abs() < 1.0e-5);
        assert!((normalized.clips[1].frames[0][0] - 99.0).abs() < 1.0e-4);
        assert!((normalized.clips[2].frames[0][0] - 3.0).abs() < 1.0e-5);
        assert_eq!(normalized.normalization.as_ref().unwrap().inv_std[31], 1.0);
    }

    #[test]
    fn normalization_rejects_missing_training_frames() {
        let input = feature_sets(vec![
            test_clip("a-test", DataSplit::Test, &[1.0]),
            test_clip("b-validation", DataSplit::Validation, &[2.0]),
        ]);
        let error = normalize_feature_sets(input).expect_err("train frames are required");
        assert!(error.to_string().contains("no training frames"));
    }

    #[test]
    fn normalization_rejects_non_finite_input() {
        let mut clip = test_clip("a-train", DataSplit::Train, &[1.0]);
        clip.frames[0][4] = f32::NAN;
        let input = feature_sets(vec![clip]);
        let error = normalize_feature_sets(input).expect_err("NaN must not reach training");
        assert!(error.to_string().contains("non-finite"));
    }

    #[test]
    fn repeated_normalization_is_rejected() {
        let input = feature_sets(vec![test_clip("a-train", DataSplit::Train, &[1.0, 3.0])]);
        let normalized = normalize_feature_sets(input).expect("first normalization");
        let error = normalize_feature_sets(normalized).expect_err("second pass must fail");
        assert!(error.to_string().contains("already been normalized"));
    }
}
