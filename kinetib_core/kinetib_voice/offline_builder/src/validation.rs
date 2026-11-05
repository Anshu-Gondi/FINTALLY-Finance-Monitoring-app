//! Validation for trained FinTally Voice vector-quantization codebooks.
//!
//! Validates artifact invariants and evaluates held-out validation frames.
//! Training frames are deliberately excluded from validation metrics.

use std::collections::HashSet;
use std::fs::{self, File};
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};

use anyhow::{ensure, Context, Result};

use crate::dataset::DataSplit;
use crate::features::{FeatureSets, FeatureVector, FEATURE_DIMENSION};
use crate::kmeans::{Codebook, CodebookSet};

const REPORT_FILE_NAME: &str = "validation_report.md";

/// Aggregate validation results for the complete codebook bundle.
#[derive(Debug, Clone)]
pub struct ValidationReport {
    pub schema_version: u32,
    pub feature_dim: usize,
    /// Seed used for deterministic K-means initialization; record it to compare runs fairly.
    pub kmeans_seed: u64,
    pub language_reports: Vec<LanguageValidationReport>,
}

/// Validation metrics for one language.
#[derive(Debug, Clone)]
pub struct LanguageValidationReport {
    pub language: String,
    pub backend: String,
    pub training_samples: u64,
    pub validation_frames: u64,
    pub cluster_count: usize,
    pub occupied_validation_clusters: usize,
    pub empty_validation_clusters: usize,
    pub converged: bool,
    pub iterations: usize,
    pub training_inertia: f64,
    pub validation_squared_error: f64,
    pub validation_mean_squared_error: f64,
    pub validation_rmse_per_dimension: f64,
    pub maximum_validation_squared_distance: f64,
}

/// Validate codebook structure and evaluate each language on held-out frames.
///
/// `features` must contain normalized features, using the same normalization
/// stored in `codebooks`. The function never fits or modifies centroids.
pub fn validate_codebooks(
    features: &FeatureSets,
    codebooks: &CodebookSet,
) -> Result<ValidationReport> {
    validate_bundle(features, codebooks)?;

    let mut reports = Vec::with_capacity(codebooks.codebooks.len());

    for codebook in &codebooks.codebooks {
        let report = validate_language(features, codebook).with_context(|| {
            format!(
                "validation failed for language {}",
                codebook.language.as_str()
            )
        })?;

        reports.push(report);
    }

    ensure!(
        !reports.is_empty(),
        "validation produced no language reports"
    );

    Ok(ValidationReport {
        schema_version: codebooks.schema_version,
        feature_dim: codebooks.feature_dim,
        kmeans_seed: codebooks.seed,
        language_reports: reports,
    })
}

/// Write a human-readable report to the configured output directory.
///
/// Writes a temporary sibling file first, then renames it into place so an
/// interrupted write does not leave a partially written final report.
pub fn write_report(report: &ValidationReport, output_dir: &Path) -> Result<PathBuf> {
    ensure!(
        !report.language_reports.is_empty(),
        "cannot write an empty validation report"
    );

    fs::create_dir_all(output_dir).with_context(|| {
        format!(
            "failed to create validation output directory: {}",
            output_dir.display()
        )
    })?;

    let final_path = output_dir.join(REPORT_FILE_NAME);
    let temp_path = output_dir.join(format!(".{REPORT_FILE_NAME}.tmp"));

    let result = write_report_file(report, &temp_path).and_then(|()| {
        fs::rename(&temp_path, &final_path).with_context(|| {
            format!(
                "failed to publish validation report at {}",
                final_path.display()
            )
        })
    });

    if result.is_err() {
        let _ = fs::remove_file(&temp_path);
    }

    result?;
    Ok(final_path)
}

fn validate_bundle(features: &FeatureSets, codebooks: &CodebookSet) -> Result<()> {
    ensure!(
        features.feature_dim == FEATURE_DIMENSION,
        "feature set dimension {} does not match native DSP dimension {}",
        features.feature_dim,
        FEATURE_DIMENSION
    );

    ensure!(
        codebooks.feature_dim == features.feature_dim,
        "codebook dimension {} does not match feature dimension {}",
        codebooks.feature_dim,
        features.feature_dim
    );

    ensure!(
        codebooks.schema_version > 0,
        "codebook schema version must be greater than zero"
    );

    let feature_normalization = features
        .normalization
        .as_ref()
        .context("feature set has no normalization metadata")?;

    ensure!(
        feature_normalization.schema_version == codebooks.normalization.schema_version,
        "feature and codebook normalization schema versions differ"
    );

    ensure!(
        feature_normalization.mean == codebooks.normalization.mean,
        "feature and codebook normalization means differ"
    );

    ensure!(
        feature_normalization.inv_std == codebooks.normalization.inv_std,
        "feature and codebook normalization inverse standard deviations differ"
    );

    ensure!(
        feature_normalization.training_frame_count == codebooks.normalization.training_frame_count,
        "feature and codebook normalization training-frame counts differ"
    );

    for dimension in 0..FEATURE_DIMENSION {
        ensure!(
            codebooks.normalization.mean[dimension].is_finite(),
            "normalization mean is non-finite at dimension {dimension}"
        );

        ensure!(
           codebooks.normalization.inv_std[dimension].is_finite()
               && codebooks.normalization.inv_std[dimension] > 0.0,
           "normalization inverse standard deviation must be finite and positive at dimension {dimension}"
       );
    }

    ensure!(
        codebooks.codebooks.len() == 3,
        "expected exactly three language codebooks (en, fr, hi); found {}",
        codebooks.codebooks.len()
    );

    let mut seen_languages = HashSet::with_capacity(codebooks.codebooks.len());

    for codebook in &codebooks.codebooks {
        ensure!(
            seen_languages.insert(codebook.language.as_str()),
            "duplicate codebook for language {}",
            codebook.language.as_str()
        );

        validate_codebook(codebook, codebooks.feature_dim)?;
    }

    Ok(())
}

fn validate_codebook(codebook: &Codebook, expected_dimensions: usize) -> Result<()> {
    ensure!(
        codebook.dimensions == expected_dimensions,
        "declared dimension {} differs from expected dimension {}",
        codebook.dimensions,
        expected_dimensions
    );

    ensure!(
        codebook.centroids.len() >= 2,
        "codebook must contain at least two centroids"
    );

    ensure!(
        codebook.training_samples >= codebook.centroids.len() as u64,
        "training sample count is smaller than the centroid count"
    );

    ensure!(
        codebook.iterations > 0,
        "codebook reports zero training iterations"
    );

    ensure!(
        codebook.inertia.is_finite() && codebook.inertia >= 0.0,
        "training inertia must be finite and non-negative"
    );

    for (centroid_index, centroid) in codebook.centroids.iter().enumerate() {
        ensure!(
            centroid.iter().all(|value| value.is_finite()),
            "centroid {centroid_index} contains a non-finite value"
        );
    }

    Ok(())
}

fn validate_language(
    features: &FeatureSets,
    codebook: &Codebook,
) -> Result<LanguageValidationReport> {
    let mut validation_frames = 0_u64;
    let mut validation_squared_error = 0.0_f64;
    let mut maximum_validation_squared_distance = 0.0_f64;

    let mut cluster_occupancy = vec![0_u64; codebook.centroids.len()];

    // Only the held-out validation split is used for these metrics.
    for clip in features
        .clips
        .iter()
        .filter(|clip| clip.split == DataSplit::Validation && clip.language == codebook.language)
    {
        for (frame_index, frame) in clip.frames.iter().enumerate() {
            ensure!(
                frame.iter().all(|value| value.is_finite()),
                "clip {:?}, frame {} contains non-finite features",
                clip.sample_id,
                frame_index
            );

            let (cluster_index, squared_distance) = nearest_centroid(frame, &codebook.centroids);

            cluster_occupancy[cluster_index] = cluster_occupancy[cluster_index]
                .checked_add(1)
                .context("validation cluster occupancy overflow")?;

            validation_frames = validation_frames
                .checked_add(1)
                .context("validation frame count overflow")?;

            validation_squared_error += squared_distance;

            ensure!(
                validation_squared_error.is_finite(),
                "validation squared-error accumulation overflowed"
            );

            maximum_validation_squared_distance =
                maximum_validation_squared_distance.max(squared_distance);
        }
    }

    ensure!(
        validation_frames > 0,
        "no held-out validation frames exist for language {}",
        codebook.language.as_str()
    );

    let occupied_validation_clusters = cluster_occupancy.iter().filter(|&&count| count > 0).count();

    let empty_validation_clusters = cluster_occupancy.len() - occupied_validation_clusters;

    let validation_mean_squared_error = validation_squared_error / validation_frames as f64;

    // Root mean squared error per feature dimension.
    let validation_rmse_per_dimension =
        (validation_mean_squared_error / codebook.dimensions as f64).sqrt();

    Ok(LanguageValidationReport {
        language: codebook.language.as_str().to_owned(),
        backend: codebook.backend.as_str().to_owned(),
        training_samples: codebook.training_samples,
        validation_frames,
        cluster_count: codebook.centroids.len(),
        occupied_validation_clusters,
        empty_validation_clusters,
        converged: codebook.converged,
        iterations: codebook.iterations,
        training_inertia: codebook.inertia,
        validation_squared_error,
        validation_mean_squared_error,
        validation_rmse_per_dimension,
        maximum_validation_squared_distance,
    })
}

/// Deterministic nearest-centroid assignment.
///
/// Ties select the first centroid. Accumulation uses f64 to reduce numerical
/// error in the evaluation metrics while preserving the original f32 vectors.
fn nearest_centroid(frame: &FeatureVector, centroids: &[FeatureVector]) -> (usize, f64) {
    let mut best_index = 0;
    let mut best_distance = f64::INFINITY;

    for (centroid_index, centroid) in centroids.iter().enumerate() {
        let mut squared_distance = 0.0_f64;

        for dimension in 0..FEATURE_DIMENSION {
            let difference = f64::from(frame[dimension]) - f64::from(centroid[dimension]);

            squared_distance += difference * difference;
        }

        if squared_distance < best_distance {
            best_distance = squared_distance;
            best_index = centroid_index;
        }
    }

    (best_index, best_distance)
}

fn write_report_file(report: &ValidationReport, path: &Path) -> Result<()> {
    let file = File::create(path)
        .with_context(|| format!("failed to create report file: {}", path.display()))?;

    let mut writer = BufWriter::new(file);

    writeln!(writer, "# FinTally Voice — Codebook Validation")?;
    writeln!(writer)?;
    writeln!(writer, "- Schema version: {}", report.schema_version)?;
    writeln!(writer, "- Feature dimension: {}", report.feature_dim)?;
    writeln!(writer, "- K-means seed: {}", report.kmeans_seed)?;
    writeln!(writer, "- Feature and centroid storage: IEEE-754 FP32")?;
    writeln!(
        writer,
        "- Validation distance arithmetic: FP64 accumulation over FP32 values"
    )?;
    writeln!(
        writer,
        "- Languages evaluated: {}",
        report.language_reports.len()
    )?;
    writeln!(writer)?;
    writeln!(writer, "## Results")?;
    writeln!(writer)?;
    writeln!(
       writer,
       "| Language | Backend | Train frames | Validation frames | Clusters | Occupied (validation) | Empty (validation) | Converged | Iterations | Training inertia | Validation MSE | Validation RMSE/dimension | Max validation squared distance |"
   )?;
    writeln!(
        writer,
        "|---|---|---:|---:|---:|---:|---:|:---:|---:|---:|---:|---:|---:|"
    )?;

    for item in &report.language_reports {
        writeln!(
            writer,
            "| {} | {} | {} | {} | {} | {} | {} | {} | {} | {:.8e} | {:.8e} | {:.8e} | {:.8e} |",
            item.language,
            item.backend,
            item.training_samples,
            item.validation_frames,
            item.cluster_count,
            item.occupied_validation_clusters,
            item.empty_validation_clusters,
            item.converged,
            item.iterations,
            item.training_inertia,
            item.validation_mean_squared_error,
            item.validation_rmse_per_dimension,
            item.maximum_validation_squared_distance,
        )?;
    }

    writeln!(writer)?;
    writeln!(
       writer,
       "Note: training inertia is measured on the training split. Validation MSE is the mean nearest-centroid squared distance per validation frame. RMSE/dimension is the square root of MSE divided by feature dimensionality."
   )?;
    writeln!(
       writer,
       "Note: an empty validation cluster means no held-out validation frame selected that centroid; it does not, by itself, prove the centroid is dead on the training distribution."
   )?;

    writer
        .flush()
        .context("failed to flush validation report")?;

    let file = writer
        .into_inner()
        .map_err(|error| error.into_error())
        .context("failed to finalize validation report")?;

    file.sync_all()
        .context("failed to sync validation report")?;

    Ok(())
}
