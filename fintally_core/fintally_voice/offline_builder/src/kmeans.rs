//! Deterministic, cache-conscious CPU/GPU K-means for FinTally Voice.
//!
//! # Design
//! - Train one codebook per supported language (`en`, `fr`, `hi`).
//! - Consume only the `train` split. Validation/test frames are never clustered.
//! - Keep feature samples in their existing FP32 storage; CPU uses references
//!   and does not flatten/clone the complete dataset.
//! - Use Rayon for independent nearest-centroid assignment and fixed-shard
//!   centroid accumulation. Each shard owns its accumulator, avoiding locks
//!   and cache-line ping-pong in the numerical hot path.
//! - Use deterministic seeded reservoir sampling + K-means++ on a bounded
//!   initialization sample, then train on every training frame.
//! - CUDA is optional. When compiled with `--features cuda`, the GPU evaluates
//!   nearest-centroid assignments while the reproducible FP64 centroid reduction
//!   remains on CPU. The feature matrix is uploaded once per language and stays
//!   resident on the device for all iterations.
//!
//! K-means is a codebook-learning algorithm, not an autoregressive transformer.
//! Continuous batching and KV caches are therefore not fabricated here: there
//! are no token requests, attention states, or autoregressive decoding steps.
//! The GPU path uses chunked pinned-memory DMA for initial upload and a large
//! parallel assignment grid each iteration, which is the relevant equivalent.

use std::cmp::{Ordering, Reverse};
use std::collections::BinaryHeap;
use std::ops::Range;

use anyhow::{anyhow, ensure, Context, Result};
use rayon::prelude::*;
use tracing::info;
#[cfg(feature = "cuda")]
use tracing::warn;

use crate::dataset::{DataSplit, LanguageId};
use crate::features::{FeatureNormalization, FeatureSets, FeatureVector, FEATURE_DIMENSION};

#[cfg(feature = "cuda")]
use crate::gpu::cuda_kmeans::CudaKMeans;

/// Version of the exported codebook data contract.
pub const CODEBOOK_SCHEMA_VERSION: u32 = 1;

/// GPU startup/transfer overhead is not worthwhile for tiny datasets. The
/// threshold is conservative and should be tuned with project benchmarks.
#[cfg(feature = "cuda")]
const CUDA_MIN_TRAINING_SAMPLES: usize = 100_000;

/// Fixed-size Rayon assignment chunks keep output writes contiguous and avoid
/// making a heap allocation per sample.
const CPU_ASSIGNMENT_CHUNK: usize = 8_192;
/// Fixed reduction shards keep floating-point accumulation grouping stable even
/// when the Rayon worker count changes. Each shard owns private FP64 accumulators.
const CENTROID_REDUCTION_SHARDS: usize = 64;

/// K-means++ has O(samples * clusters * dimensions) initialization cost. Limit
/// that cost with a deterministic reservoir; full training still sees all rows.
const MAX_INITIALIZATION_SAMPLES: usize = 16_384;

/// Relative inertia improvement below this threshold is considered stable.
/// It is not sufficient on its own: centroid movement and assignment churn
/// must also remain within the configured bounds for several consecutive rounds.
const RELATIVE_INERTIA_TOLERANCE: f64 = 1.0e-4;

/// Candidate absolute L2 movement tolerance for normalized feature vectors.
/// Validate this setting against held-out metrics before treating it as final.
const CENTROID_SHIFT_TOLERANCE: f64 = 1.0e-2;
const CENTROID_SHIFT_SQUARED_TOLERANCE: f64 = CENTROID_SHIFT_TOLERANCE * CENTROID_SHIFT_TOLERANCE;

/// Permit at most 0.1% of assignments to change when the objective is stable.
const MAX_CHANGED_ASSIGNMENT_RATIO: f64 = 1.0e-3;

/// Require the stopping criteria to hold over multiple consecutive iterations.
const REQUIRED_STABLE_ITERATIONS: usize = 3;

const REQUIRED_LANGUAGES: [LanguageId; 3] = [LanguageId::En, LanguageId::Fr, LanguageId::Hi];

/// Parameters supplied by `main.rs`. This keeps the current CLI contract intact.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct KMeansConfig {
    pub dimensions: usize,
    pub clusters: usize,
    pub max_iterations: usize,
    pub seed: u64,
    /// 0 means use Rayon's configured/default pool size.
    pub workers: usize,
}

/// Which implementation trained a codebook. Kept in the artifact metadata so
/// CPU and CUDA outputs can be compared rather than silently conflated.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ComputeBackend {
    RayonCpu,
    Cuda,
}

impl ComputeBackend {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::RayonCpu => "rayon-cpu",
            Self::Cuda => "cuda-nearest-assignment+rayon-reduction",
        }
    }
}

/// One language-specific vector-quantization codebook.
///
/// Centroids are in the normalized feature space. Export the associated
/// `FeatureNormalization` from `CodebookSet` along with these centroids; a
/// runtime must apply the same transform before nearest-centroid lookup.
#[derive(Debug, Clone)]
pub struct Codebook {
    pub language: LanguageId,
    pub dimensions: usize,
    pub centroids: Vec<FeatureVector>,
    pub training_samples: u64,
    pub iterations: usize,
    /// True when the exact-zero-error shortcut or the configured approximate
    /// convergence criteria are satisfied; hitting the iteration limit alone
    /// never sets this flag.
    pub converged: bool,
    /// Sum of nearest-centroid squared distances on the train split after the
    /// final centroids are produced. It is not a validation/test metric.
    pub inertia: f64,
    pub backend: ComputeBackend,
}

/// Complete codebook artifact bundle, including the normalization needed to
/// interpret the centroids. `main.rs` can continue to call `.len()` unchanged.
#[derive(Debug, Clone)]
pub struct CodebookSet {
    pub schema_version: u32,
    pub feature_dim: usize,
    pub seed: u64,
    pub normalization: FeatureNormalization,
    pub codebooks: Vec<Codebook>,
}

impl CodebookSet {
    pub fn len(&self) -> usize {
        self.codebooks.len()
    }

    pub fn is_empty(&self) -> bool {
        self.codebooks.is_empty()
    }

    pub fn iter(&self) -> impl Iterator<Item = &Codebook> {
        self.codebooks.iter()
    }

    /// Borrow codebooks as a slice without cloning them.
    pub fn as_slice(&self) -> &[Codebook] {
        self.codebooks.as_slice()
    }

    pub fn get(&self, language: LanguageId) -> Option<&Codebook> {
        self.codebooks
            .iter()
            .find(|codebook| codebook.language == language)
    }
}

// Transitional convenience for validators/exporters that currently accept
// `&Vec<Codebook>` or `&[Codebook]`. New code should prefer the explicit
// `CodebookSet` so the normalization contract cannot be accidentally omitted.
impl std::ops::Deref for CodebookSet {
    type Target = Vec<Codebook>;

    fn deref(&self) -> &Self::Target {
        &self.codebooks
    }
}

/// Train the three supported language codebooks from normalized training data.
///
/// This signature matches the orchestration already present in `main.rs`:
/// `train_codebooks(&normalized, &KMeansConfig) -> Result<CodebookSet>`.
pub fn train_codebooks(features: &FeatureSets, config: &KMeansConfig) -> Result<CodebookSet> {
    validate_training_input(features, config)?;

    // Build one pool once, not once per iteration. Repeated pool construction
    // would add launch/setup overhead and makes worker placement less predictable.
    if config.workers == 0 {
        train_codebooks_inner(features, config)
    } else {
        let pool = rayon::ThreadPoolBuilder::new()
            .num_threads(config.workers)
            .thread_name(|index| format!("fintally-kmeans-{index}"))
            .build()
            .with_context(|| format!("failed to create {}-worker K-means pool", config.workers))?;
        pool.install(|| train_codebooks_inner(features, config))
    }
}

fn validate_training_input(features: &FeatureSets, config: &KMeansConfig) -> Result<()> {
    ensure!(
        config.dimensions == FEATURE_DIMENSION && features.feature_dim == FEATURE_DIMENSION,
        "K-means dimension must match the native DSP ABI: expected {}, configured {}, feature set {}",
        FEATURE_DIMENSION,
        config.dimensions,
        features.feature_dim
    );
    ensure!(config.clusters >= 2, "K-means needs at least two clusters");
    ensure!(
        config.clusters <= u32::MAX as usize,
        "cluster count exceeds the assignment representation"
    );
    ensure!(
        config.max_iterations > 0,
        "max_iterations must be greater than zero"
    );
    ensure!(
        features.normalization.is_some(),
        "feature sets must be normalized before codebook training"
    );
    ensure!(!features.clips.is_empty(), "feature set has no clips");

    for language in REQUIRED_LANGUAGES {
        let count = features
            .clips_for_split(DataSplit::Train)
            .filter(|clip| clip.language == language)
            .map(|clip| clip.frames.len())
            .sum::<usize>();
        ensure!(
            count >= config.clusters,
            "language {language} has {count} training frames; codebook_size={} requires at least that many frames",
            config.clusters
        );
    }

    for clip in &features.clips {
        ensure!(
            clip.frames.iter().flatten().all(|value| value.is_finite()),
            "feature clip {:?} contains non-finite values",
            clip.sample_id
        );
    }

    Ok(())
}

fn train_codebooks_inner(features: &FeatureSets, config: &KMeansConfig) -> Result<CodebookSet> {
    let normalization = features
        .normalization
        .clone()
        .ok_or_else(|| anyhow!("feature normalization state is missing"))?;

    #[cfg(feature = "cuda")]
    let mut cuda_engine: Option<CudaKMeans> = None;
    #[cfg(feature = "cuda")]
    let mut cuda_init_attempted = false;

    let mut codebooks = Vec::with_capacity(REQUIRED_LANGUAGES.len());

    for language in REQUIRED_LANGUAGES {
        // Only training-split frames enter clustering. Retain references into the
        // feature store rather than flattening/copying the full FP32 matrix.
        let points: Vec<&FeatureVector> = features
            .clips_for_split(DataSplit::Train)
            .filter(|clip| clip.language == language)
            .flat_map(|clip| clip.frames.iter())
            .collect();
        ensure!(
            points.len() >= config.clusters,
            "not enough training frames for {language}"
        );

        #[cfg(feature = "cuda")]
        let mut gpu_dataset = None;
        #[cfg(feature = "cuda")]
        if points.len() >= CUDA_MIN_TRAINING_SAMPLES {
            if !cuda_init_attempted {
                cuda_init_attempted = true;
                match CudaKMeans::new() {
                    Ok(engine) => {
                        info!("CUDA K-means backend initialized");
                        cuda_engine = Some(engine);
                    }
                    Err(error) => {
                        warn!(
                            error = %error,
                            "CUDA initialization failed; this run will use the Rayon CPU backend"
                        );
                    }
                }
            }

            if let Some(engine) = cuda_engine.as_ref() {
                match engine.prepare_dataset(&points, config.clusters) {
                    Ok(dataset) => gpu_dataset = Some(dataset),
                    Err(error) => warn!(
                        language = %language,
                        error = %error,
                        "CUDA dataset upload failed or did not fit available VRAM; falling back to CPU for this language"
                    ),
                }
            }
        }

        let seed = seed_for_language(config.seed, language);
        let initial_centroids = initialize_centroids(&points, config.clusters, seed)
            .with_context(|| format!("K-means++ initialization failed for {language}"))?;

        let codebook = {
            #[cfg(feature = "cuda")]
            {
                if let (Some(engine), Some(dataset)) = (cuda_engine.as_ref(), gpu_dataset) {
                    let mut assigner = CudaAssigner { engine, dataset };
                    fit_language(language, &points, config, initial_centroids, &mut assigner)?
                } else {
                    let mut assigner = CpuAssigner;
                    fit_language(language, &points, config, initial_centroids, &mut assigner)?
                }
            }
            #[cfg(not(feature = "cuda"))]
            {
                let mut assigner = CpuAssigner;
                fit_language(language, &points, config, initial_centroids, &mut assigner)?
            }
        };

        info!(
            language = %codebook.language,
            backend = codebook.backend.as_str(),
            training_samples = codebook.training_samples,
            clusters = codebook.centroids.len(),
            iterations = codebook.iterations,
            converged = codebook.converged,
            inertia = codebook.inertia,
            "language codebook trained"
        );
        codebooks.push(codebook);
    }

    Ok(CodebookSet {
        schema_version: CODEBOOK_SCHEMA_VERSION,
        feature_dim: FEATURE_DIMENSION,
        seed: config.seed,
        normalization,
        codebooks,
    })
}

/// Internal contract implemented by the Rayon and CUDA nearest-assignment paths.
/// It writes into caller-owned reusable arrays so there is no per-iteration
/// allocation of N labels and N distances.
trait AssignmentEngine {
    fn assign(
        &mut self,
        points: &[&FeatureVector],
        centroids: &[FeatureVector],
        output: &mut AssignmentBuffers,
    ) -> Result<()>;

    fn backend(&self) -> ComputeBackend;
}

struct CpuAssigner;

impl AssignmentEngine for CpuAssigner {
    fn assign(
        &mut self,
        points: &[&FeatureVector],
        centroids: &[FeatureVector],
        output: &mut AssignmentBuffers,
    ) -> Result<()> {
        assign_cpu(points, centroids, output)
    }

    fn backend(&self) -> ComputeBackend {
        ComputeBackend::RayonCpu
    }
}

#[cfg(feature = "cuda")]
struct CudaAssigner<'a> {
    engine: &'a CudaKMeans,
    dataset: crate::gpu::cuda_kmeans::CudaDataset,
}

#[cfg(feature = "cuda")]
impl AssignmentEngine for CudaAssigner<'_> {
    fn assign(
        &mut self,
        _points: &[&FeatureVector],
        centroids: &[FeatureVector],
        output: &mut AssignmentBuffers,
    ) -> Result<()> {
        let (labels, distances) = self.engine.assign(&mut self.dataset, centroids)?;
        ensure!(
            labels.len() == output.labels.len(),
            "CUDA returned an invalid label count"
        );
        ensure!(
            distances.len() == output.squared_distances.len(),
            "CUDA returned an invalid distance count"
        );

        // The device matrix and pinned DMA output buffers are reused. This copy
        // lands in the same CPU arrays used by the deterministic reduction code.
        output.labels.copy_from_slice(labels);
        output.squared_distances.copy_from_slice(distances);
        Ok(())
    }

    fn backend(&self) -> ComputeBackend {
        ComputeBackend::Cuda
    }
}

struct AssignmentBuffers {
    labels: Vec<u32>,
    squared_distances: Vec<f32>,
}

impl AssignmentBuffers {
    fn new(samples: usize) -> Self {
        Self {
            labels: vec![0; samples],
            squared_distances: vec![0.0; samples],
        }
    }
}

fn fit_language<A: AssignmentEngine>(
    language: LanguageId,
    points: &[&FeatureVector],
    config: &KMeansConfig,
    mut centroids: Vec<FeatureVector>,
    assigner: &mut A,
) -> Result<Codebook> {
    ensure!(
        points.len() >= config.clusters,
        "training sample count is less than cluster count"
    );
    ensure!(
        centroids.len() == config.clusters,
        "initializer returned the wrong centroid count"
    );

    let mut assignments = AssignmentBuffers::new(points.len());
    let mut previous_labels = vec![u32::MAX; points.len()];
    let mut previous_inertia: Option<f64> = None;
    let mut converged = false;
    let mut iterations_run = 0;
    let mut stable_iterations = 0_usize;

    for iteration in 0..config.max_iterations {
        assigner
            .assign(points, &centroids, &mut assignments)
            .with_context(|| {
                format!(
                    "nearest-centroid assignment failed for {language}, iteration {}",
                    iteration + 1
                )
            })?;
        validate_assignments(&assignments, config.clusters)?;

        let inertia = deterministic_inertia(&assignments.squared_distances);

        // If every point lies exactly on an assigned centroid, updating empty
        // clusters would only spend time ranking identical zero-error points.
        if inertia == 0.0 {
            iterations_run = iteration + 1;
            converged = true;
            break;
        }

        let changed_assignments = assignments
            .labels
            .iter()
            .zip(previous_labels.iter())
            .filter(|(new, old)| new != old)
            .count();

        let (next_centroids, max_shift_squared, empty_training_clusters) = update_centroids(
            points,
            &assignments.labels,
            &assignments.squared_distances,
            &centroids,
            config.clusters,
        )?;

        let relative_improvement = previous_inertia.map(|previous| {
            if previous <= f64::MIN_POSITIVE {
                0.0
            } else {
                (previous - inertia) / previous
            }
        });

        previous_labels.copy_from_slice(&assignments.labels);
        previous_inertia = Some(inertia);
        centroids = next_centroids;
        iterations_run = iteration + 1;

        // A small relative objective change is a convergence signal only when
        // centroid movement and assignment churn are also within configured bounds.
        // Negative improvement is not treated as stable; it can indicate numerical
        // differences between iterations or assignment backends.
        let objective_stable = relative_improvement
            .is_some_and(|delta| delta >= 0.0 && delta <= RELATIVE_INERTIA_TOLERANCE);

        let changed_assignment_ratio = changed_assignments as f64 / points.len() as f64;
        let centers_stable = max_shift_squared <= CENTROID_SHIFT_SQUARED_TOLERANCE;
        let assignment_behavior_stable = changed_assignments == 0
            || (objective_stable && changed_assignment_ratio <= MAX_CHANGED_ASSIGNMENT_RATIO);

        // Require approximate convergence conditions to hold for several
        // consecutive iterations. This helps prevent a single transient low-shift
        // iteration (for example, after an empty-cluster reseed) from stopping training.
        if centers_stable && assignment_behavior_stable {
            stable_iterations += 1;
        } else {
            stable_iterations = 0;
        }

        info!(
            language = %language,
            iteration = iteration + 1,
            inertia,
            relative_improvement = ?relative_improvement,
            changed_assignments,
            changed_assignment_ratio,
            empty_training_clusters,
            max_centroid_shift = max_shift_squared.sqrt(),
            centroid_shift_squared = max_shift_squared,
            objective_stable,
            centers_stable,
            assignment_behavior_stable,
            stable_iterations,
            "K-means convergence diagnostics"
        );

        if stable_iterations >= REQUIRED_STABLE_ITERATIONS {
            converged = true;
            break;
        }
    }

    // The final loop body updates the centers after computing the assignments.
    // Reassign once so the exported inertia exactly describes the returned
    // centers instead of the previous iteration's centers.
    assigner
        .assign(points, &centroids, &mut assignments)
        .with_context(|| format!("final inertia pass failed for {language}"))?;
    validate_assignments(&assignments, config.clusters)?;
    let final_inertia = deterministic_inertia(&assignments.squared_distances);

    ensure!(
        centroids.iter().flatten().all(|value| value.is_finite()),
        "K-means produced non-finite centroids for {language}"
    );

    Ok(Codebook {
        language,
        dimensions: FEATURE_DIMENSION,
        centroids,
        training_samples: u64::try_from(points.len())
            .context("training sample count does not fit u64")?,
        iterations: iterations_run,
        converged,
        inertia: final_inertia,
        backend: assigner.backend(),
    })
}

fn assign_cpu(
    points: &[&FeatureVector],
    centroids: &[FeatureVector],
    output: &mut AssignmentBuffers,
) -> Result<()> {
    ensure!(
        points.len() == output.labels.len(),
        "CPU assignment buffer length mismatch"
    );
    ensure!(
        centroids.len() >= 2,
        "CPU assignment needs at least two centroids"
    );

    // Each worker writes a disjoint contiguous output slice. No locks, atomics,
    // cross-thread shared counters, or per-sample allocations occur here.
    points
        .par_chunks(CPU_ASSIGNMENT_CHUNK)
        .zip(output.labels.par_chunks_mut(CPU_ASSIGNMENT_CHUNK))
        .zip(
            output
                .squared_distances
                .par_chunks_mut(CPU_ASSIGNMENT_CHUNK),
        )
        .for_each(|((point_chunk, labels), distances)| {
            for (index, point) in point_chunk.iter().enumerate() {
                let mut best_distance = f32::INFINITY;
                let mut best_cluster = 0_usize;

                for (cluster, centroid) in centroids.iter().enumerate() {
                    let distance = squared_distance_f32(point, centroid);
                    // Strict < keeps ties deterministic: the lowest centroid ID wins.
                    if distance < best_distance {
                        best_distance = distance;
                        best_cluster = cluster;
                    }
                }

                labels[index] = best_cluster as u32;
                distances[index] = best_distance;
            }
        });

    ensure!(
        output
            .squared_distances
            .iter()
            .all(|distance| distance.is_finite() && *distance >= 0.0),
        "CPU assignment generated a non-finite squared distance"
    );

    Ok(())
}

fn validate_assignments(assignments: &AssignmentBuffers, cluster_count: usize) -> Result<()> {
    ensure!(
        assignments
            .labels
            .iter()
            .all(|label| (*label as usize) < cluster_count),
        "assignment returned a cluster ID outside the codebook"
    );
    ensure!(
        assignments
            .squared_distances
            .iter()
            .all(|distance| distance.is_finite() && *distance >= 0.0),
        "assignment returned a non-finite or negative distance"
    );
    Ok(())
}

fn deterministic_inertia(distances: &[f32]) -> f64 {
    // Fixed contiguous chunks with ordered reduction: deterministic without
    // serializing the entire 10-million-frame distance pass.
    distances
        .par_chunks(CPU_ASSIGNMENT_CHUNK)
        .map(|chunk| chunk.iter().map(|value| f64::from(*value)).sum::<f64>())
        .collect::<Vec<_>>()
        .into_iter()
        .sum()
}

fn update_centroids(
    points: &[&FeatureVector],
    labels: &[u32],
    distances: &[f32],
    previous_centroids: &[FeatureVector],
    clusters: usize,
) -> Result<(Vec<FeatureVector>, f64, usize)> {
    ensure!(
        points.len() == labels.len(),
        "centroid update sample/label length mismatch"
    );
    ensure!(
        points.len() == distances.len(),
        "centroid update sample/distance length mismatch"
    );

    // Use a small, fixed number of shards instead of one K*D accumulator per
    // sample/chunk. This bounds scratch memory by O(shard_count * K * D),
    // independent of the total number of training frames.
    // FP64 local sums reduce order-of-addition error; stored centroids stay FP32.
    // Keep shard boundaries independent of Rayon thread count. That makes the
    // reduction order reproducible across worker-pool sizes; Rayon may schedule
    // these same fixed shards on different workers without changing their sums.
    let shard_count = CENTROID_REDUCTION_SHARDS.min(points.len()).max(1);
    let base_shard_len = points.len() / shard_count;
    let extra_rows = points.len() % shard_count;
    let ranges: Vec<Range<usize>> = (0..shard_count)
        .map(|shard| {
            // Distribute remainder rows across the first shards without an
            // intermediate allocation or a multiplication that can overflow.
            let start = shard * base_shard_len + shard.min(extra_rows);
            let shard_len = base_shard_len + usize::from(shard < extra_rows);
            start..start + shard_len
        })
        .collect();

    let partials: Vec<PartialCentroids> = ranges
        .into_par_iter()
        .map(|range| {
            let mut sums = vec![0.0_f64; clusters * FEATURE_DIMENSION];
            let mut counts = vec![0_u64; clusters];
            for index in range {
                let cluster = labels[index] as usize;
                counts[cluster] += 1;
                let base = cluster * FEATURE_DIMENSION;
                for dimension in 0..FEATURE_DIMENSION {
                    sums[base + dimension] += f64::from(points[index][dimension]);
                }
            }
            PartialCentroids { sums, counts }
        })
        .collect();

    // `collect` preserves indexed range order. Combine partial sums serially in
    // that order so changing work-stealing schedules doesn't alter reduction order.
    let mut sums = vec![0.0_f64; clusters * FEATURE_DIMENSION];
    let mut counts = vec![0_u64; clusters];
    for partial in partials {
        for (target, value) in sums.iter_mut().zip(partial.sums.iter()) {
            *target += value;
        }
        for (target, value) in counts.iter_mut().zip(partial.counts.iter()) {
            *target += value;
        }
    }

    let mut updated = previous_centroids.to_vec();
    let mut empty_clusters = Vec::new();
    for cluster in 0..clusters {
        if counts[cluster] == 0 {
            empty_clusters.push(cluster);
            continue;
        }

        let base = cluster * FEATURE_DIMENSION;
        let count = counts[cluster] as f64;
        for dimension in 0..FEATURE_DIMENSION {
            let value = (sums[base + dimension] / count) as f32;
            ensure!(
                value.is_finite(),
                "centroid update overflowed at cluster {cluster}, dimension {dimension}"
            );
            updated[cluster][dimension] = value;
        }
    }

    // Keep this count for per-iteration diagnostics. A nonzero value indicates
    // that at least one centroid is being reseeded instead of computed from its
    // assigned training samples during this update.
    let empty_training_clusters = empty_clusters.len();

    if !empty_clusters.is_empty() {
        // Empty clusters are reseeded from distinct high-error samples. A bounded
        // heap avoids sorting all N distances; the common no-empty-cluster path
        // pays only an empty vector check.
        let replacements = farthest_sample_indices(distances, empty_clusters.len());
        ensure!(
            replacements.len() == empty_clusters.len(),
            "not enough samples to repair empty clusters"
        );
        for (cluster, sample_index) in empty_clusters.into_iter().zip(replacements) {
            updated[cluster] = *points[sample_index];
        }
    }

    let max_shift_squared = updated
        .iter()
        .zip(previous_centroids.iter())
        .map(|(next, previous)| {
            next.iter()
                .zip(previous.iter())
                .map(|(lhs, rhs)| {
                    let delta = f64::from(*lhs) - f64::from(*rhs);
                    delta * delta
                })
                .sum::<f64>()
        })
        .fold(0.0_f64, f64::max);

    Ok((updated, max_shift_squared, empty_training_clusters))
}

struct PartialCentroids {
    sums: Vec<f64>,
    counts: Vec<u64>,
}

#[derive(Debug, Clone, Copy)]
struct DistanceIndex {
    distance: f32,
    index: usize,
}

impl PartialEq for DistanceIndex {
    fn eq(&self, other: &Self) -> bool {
        self.distance.to_bits() == other.distance.to_bits() && self.index == other.index
    }
}
impl Eq for DistanceIndex {}
impl PartialOrd for DistanceIndex {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}
impl Ord for DistanceIndex {
    fn cmp(&self, other: &Self) -> Ordering {
        self.distance
            .total_cmp(&other.distance)
            .then_with(|| self.index.cmp(&other.index))
    }
}

fn farthest_sample_indices(distances: &[f32], requested: usize) -> Vec<usize> {
    if requested == 0 || distances.is_empty() {
        return Vec::new();
    }

    // Reverse makes BinaryHeap's head the smallest candidate among the current
    // top-N. Every sample index is visited once, so replacements are distinct.
    let mut heap: BinaryHeap<Reverse<DistanceIndex>> = BinaryHeap::with_capacity(requested);
    for (index, distance) in distances.iter().copied().enumerate() {
        let candidate = DistanceIndex { distance, index };
        if heap.len() < requested {
            heap.push(Reverse(candidate));
        } else if heap
            .peek()
            .is_some_and(|Reverse(smallest)| candidate > *smallest)
        {
            heap.pop();
            heap.push(Reverse(candidate));
        }
    }

    let mut selected: Vec<DistanceIndex> = heap.into_iter().map(|Reverse(item)| item).collect();
    selected.sort_by(|left, right| {
        right
            .distance
            .total_cmp(&left.distance)
            .then_with(|| left.index.cmp(&right.index))
    });
    selected.into_iter().map(|item| item.index).collect()
}

fn initialize_centroids(
    points: &[&FeatureVector],
    clusters: usize,
    seed: u64,
) -> Result<Vec<FeatureVector>> {
    ensure!(
        points.len() >= clusters,
        "K-means++ requires at least one sample per cluster"
    );

    // The sample must never contain fewer rows than the requested K, even if
    // a future configuration chooses more than the normal initialization cap.
    let target = clusters
        .saturating_mul(128)
        .clamp(4_096, MAX_INITIALIZATION_SAMPLES)
        .max(clusters)
        .min(points.len());
    let mut rng = SplitMix64::new(seed);
    let candidates = reservoir_sample(points, target, &mut rng);
    ensure!(
        candidates.len() >= clusters,
        "initialization sample is smaller than cluster count"
    );

    let mut centroids = Vec::with_capacity(clusters);
    let mut chosen = vec![false; candidates.len()];
    let mut nearest_squared = vec![f64::INFINITY; candidates.len()];

    let first = rng.index(candidates.len());
    chosen[first] = true;
    centroids.push(*candidates[first]);

    while centroids.len() < clusters {
        let latest = centroids
            .last()
            .expect("the first centroid is always inserted");
        let mut total_weight = 0.0_f64;
        for (index, candidate) in candidates.iter().enumerate() {
            let distance = squared_distance_f64(candidate, latest);
            if distance < nearest_squared[index] {
                nearest_squared[index] = distance;
            }
            if !chosen[index] {
                total_weight += nearest_squared[index];
            }
        }

        let next = if total_weight.is_finite() && total_weight > f64::MIN_POSITIVE {
            let threshold = rng.unit_f64() * total_weight;
            let mut cumulative = 0.0_f64;
            let mut selected = None;
            for (index, distance) in nearest_squared.iter().copied().enumerate() {
                if chosen[index] {
                    continue;
                }
                cumulative += distance;
                if cumulative >= threshold && distance > 0.0 {
                    selected = Some(index);
                    break;
                }
            }
            selected.unwrap_or_else(|| {
                chosen
                    .iter()
                    .position(|value| !value)
                    .expect("candidate count >= clusters")
            })
        } else {
            // All remaining candidates are identical or numerically zero-distance.
            // Choose an unused sample deterministically instead of dividing by zero.
            chosen
                .iter()
                .position(|value| !value)
                .expect("candidate count >= clusters")
        };

        chosen[next] = true;
        centroids.push(*candidates[next]);
    }

    Ok(centroids)
}

fn reservoir_sample<'a>(
    points: &[&'a FeatureVector],
    target: usize,
    rng: &mut SplitMix64,
) -> Vec<&'a FeatureVector> {
    let mut sample = points[..target].to_vec();
    for (index, point) in points.iter().enumerate().skip(target) {
        let replacement = rng.index(index + 1);
        if replacement < target {
            sample[replacement] = *point;
        }
    }
    sample
}

/// Small project-owned RNG keeps initialization stable across rand crate version
/// changes and avoids making the clustering contract depend on crate internals.
struct SplitMix64(u64);

impl SplitMix64 {
    fn new(seed: u64) -> Self {
        Self(seed)
    }

    fn next_u64(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut value = self.0;
        value = (value ^ (value >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        value = (value ^ (value >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        value ^ (value >> 31)
    }

    fn index(&mut self, upper_exclusive: usize) -> usize {
        assert!(upper_exclusive > 0, "random index range must be non-empty");
        let upper = upper_exclusive as u64;
        let rejection_threshold = upper.wrapping_neg() % upper;
        loop {
            let value = self.next_u64();
            if value >= rejection_threshold {
                return (value % upper) as usize;
            }
        }
    }

    fn unit_f64(&mut self) -> f64 {
        // Exactly 53 random mantissa bits in [0, 1); stable on every platform.
        ((self.next_u64() >> 11) as f64) * (1.0 / ((1_u64 << 53) as f64))
    }
}

fn seed_for_language(seed: u64, language: LanguageId) -> u64 {
    // Stable FNV-1a language salt: changing iteration order won't give each
    // language a different random stream accidentally.
    let mut hash = 0xCBF2_9CE4_8422_2325_u64;
    for byte in language.as_str().bytes() {
        hash ^= u64::from(byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01B3);
    }
    seed ^ hash
}

#[inline]
fn squared_distance_f32(lhs: &[f32; FEATURE_DIMENSION], rhs: &[f32; FEATURE_DIMENSION]) -> f32 {
    let mut sum = 0.0_f32;
    for dimension in 0..FEATURE_DIMENSION {
        let difference = lhs[dimension] - rhs[dimension];
        sum += difference * difference;
    }
    sum
}

#[inline]
fn squared_distance_f64(lhs: &[f32; FEATURE_DIMENSION], rhs: &[f32; FEATURE_DIMENSION]) -> f64 {
    let mut sum = 0.0_f64;
    for dimension in 0..FEATURE_DIMENSION {
        let difference = f64::from(lhs[dimension]) - f64::from(rhs[dimension]);
        sum += difference * difference;
    }
    sum
}

#[cfg(test)]
mod tests {
    use super::*;

    fn frame(first_dimension: f32, second_dimension: f32) -> FeatureVector {
        let mut result = [0.0_f32; FEATURE_DIMENSION];
        result[0] = first_dimension;
        result[1] = second_dimension;
        result
    }

    fn test_config() -> KMeansConfig {
        KMeansConfig {
            dimensions: FEATURE_DIMENSION,
            clusters: 2,
            max_iterations: 20,
            seed: 0xF1_0C_A11,
            workers: 1,
        }
    }

    #[test]
    fn cpu_kmeans_separates_two_obvious_groups() {
        let owned = vec![
            frame(-1.0, 0.0),
            frame(-1.1, 0.1),
            frame(-0.9, -0.1),
            frame(10.0, 0.0),
            frame(10.1, 0.1),
            frame(9.9, -0.1),
        ];
        let points: Vec<&FeatureVector> = owned.iter().collect();
        let mut assigner = CpuAssigner;
        let codebook = fit_language(
            LanguageId::En,
            &points,
            &test_config(),
            initialize_centroids(&points, 2, 9).unwrap(),
            &mut assigner,
        )
        .unwrap();

        assert_eq!(codebook.centroids.len(), 2);
        assert_eq!(codebook.training_samples, 6);
        assert!(codebook.inertia.is_finite());
        assert!(codebook.inertia < 1.0);
        assert!(codebook
            .centroids
            .iter()
            .flatten()
            .all(|value| value.is_finite()));
    }

    #[test]
    fn public_training_builds_one_codebook_per_language_from_train_only() {
        use crate::dataset::{DataSplit, DatasetKind};
        use crate::features::{FeatureClip, FeatureNormalization};

        let mut clips = Vec::new();
        for (language_index, language) in REQUIRED_LANGUAGES.into_iter().enumerate() {
            let offset = language_index as f32 * 100.0;
            let frames = vec![
                frame(offset, 0.0),
                frame(offset + 0.1, 0.0),
                frame(offset + 10.0, 0.0),
                frame(offset + 10.1, 0.0),
            ];
            clips.push(FeatureClip {
                sample_id: format!("{language}-train"),
                language,
                dataset: if language == LanguageId::Hi {
                    DatasetKind::CommonVoice
                } else {
                    DatasetKind::Sps
                },
                split: DataSplit::Train,
                duration_ms: 40,
                source_duration_ms: 40,
                client_id: Some(format!("{language}-speaker")),
                source_split: Some("train".to_owned()),
                transcript: Some("example transcript".to_owned()),
                prompt: None,
                frames,
            });
            // An extreme validation frame must not enter centroid initialization,
            // centroid updates, or the recorded training sample count.
            clips.push(FeatureClip {
                sample_id: format!("{language}-validation"),
                language,
                dataset: if language == LanguageId::Hi {
                    DatasetKind::CommonVoice
                } else {
                    DatasetKind::Sps
                },
                split: DataSplit::Validation,
                duration_ms: 10,
                source_duration_ms: 10,
                client_id: Some(format!("{language}-validation-speaker")),
                source_split: Some("dev".to_owned()),
                transcript: Some("held out".to_owned()),
                prompt: None,
                frames: vec![frame(1_000_000.0, 1_000_000.0)],
            });
        }

        let features = FeatureSets {
            sample_rate: 16_000,
            feature_dim: FEATURE_DIMENSION,
            clips,
            normalization: Some(FeatureNormalization {
                schema_version: 1,
                mean: [0.0; FEATURE_DIMENSION],
                inv_std: [1.0; FEATURE_DIMENSION],
                training_frame_count: 12,
            }),
        };
        let config = KMeansConfig {
            dimensions: FEATURE_DIMENSION,
            clusters: 2,
            max_iterations: 20,
            seed: 42,
            workers: 2,
        };

        let trained = train_codebooks(&features, &config).expect("training should succeed");
        assert_eq!(trained.len(), 3);
        for language in REQUIRED_LANGUAGES {
            let codebook = trained
                .get(language)
                .expect("all supported languages trained");
            assert_eq!(codebook.training_samples, 4);
            assert_eq!(codebook.centroids.len(), 2);
            assert!(codebook.inertia.is_finite());
            assert!(codebook.centroids.iter().flatten().all(|v| v.is_finite()));
        }
    }

    #[test]
    fn seeded_initialization_is_repeatable() {
        let owned = vec![
            frame(0.0, 1.0),
            frame(1.0, 2.0),
            frame(9.0, 8.0),
            frame(10.0, 9.0),
        ];
        let points: Vec<&FeatureVector> = owned.iter().collect();
        let first = initialize_centroids(&points, 2, 1234).unwrap();
        let second = initialize_centroids(&points, 2, 1234).unwrap();
        assert_eq!(first, second);
    }

    #[test]
    fn farthest_sample_selection_never_repeats_an_index() {
        let distances = [0.1_f32, 7.0, 3.0, 7.0, 1.0];
        let selected = farthest_sample_indices(&distances, 3);
        assert_eq!(selected, vec![1, 3, 2]);
    }

    #[test]
    fn assignment_ties_choose_the_lowest_centroid_id() {
        let points_owned = [frame(0.0, 0.0)];
        let points: Vec<&FeatureVector> = points_owned.iter().collect();
        let centroids = [frame(-1.0, 0.0), frame(1.0, 0.0)];
        let mut buffers = AssignmentBuffers::new(1);
        assign_cpu(&points, &centroids, &mut buffers).unwrap();
        assert_eq!(buffers.labels[0], 0);
    }
}
