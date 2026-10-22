//! Dataset discovery and selection for the Fintally Voice offline builder.
//!
//! Responsibilities:
//!
//! - recursively discover dataset metadata/audio files
//! - resolve datasets without depending on release-date directory names
//! - expose a language-independent TrainingClip representation
//! - enforce deterministic dataset selection
//! - limit selected audio by ACTUAL metadata duration
//! - discover already-prepared WAV datasets
//!
//! The module deliberately does NOT decode audio.
//! Audio decoding/resampling belongs in `audio/`.

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::fmt;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::{Component, Path, PathBuf};

use serde::{Deserialize, Serialize};

use anyhow::{bail, Context, Result};
use hound::{SampleFormat, WavReader};
use tracing::{info, warn};
use walkdir::WalkDir;

pub mod common_voice;
pub mod sps;

// =============================================================================
// LANGUAGE
// =============================================================================

/// Languages currently supported by the offline builder.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum LanguageId {
    En,
    Fr,
    Hi,
}

impl LanguageId {
    /// Languages enabled by the current dataset contract.
    pub const ALL: [LanguageId; 3] = [LanguageId::En, LanguageId::Fr, LanguageId::Hi];

    pub const fn as_str(self) -> &'static str {
        match self {
            LanguageId::En => "en",
            LanguageId::Fr => "fr",
            LanguageId::Hi => "hi",
        }
    }

    pub fn parse(value: &str) -> Result<Self> {
        match value {
            "en" => Ok(Self::En),
            "fr" => Ok(Self::Fr),
            "hi" => Ok(Self::Hi),
            _ => bail!("unsupported language in prepared manifest: {value:?}"),
        }
    }
}

impl fmt::Display for LanguageId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

// =============================================================================
// DATASET KIND
// =============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DatasetKind {
    Sps,
    CommonVoice,
}

impl DatasetKind {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Sps => "sps",
            Self::CommonVoice => "common-voice",
        }
    }

    pub fn parse(value: &str) -> Result<Self> {
        match value {
            "sps" => Ok(Self::Sps),
            "common-voice" => Ok(Self::CommonVoice),
            _ => bail!("unsupported dataset kind in prepared manifest: {value:?}"),
        }
    }
}

impl fmt::Display for DatasetKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

/// Effective, speaker-grouped split used by downstream training and validation.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum DataSplit {
    Train,
    Validation,
    Test,
}

impl DataSplit {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Train => "train",
            Self::Validation => "validation",
            Self::Test => "test",
        }
    }

    pub fn parse(value: &str) -> Result<Self> {
        match value {
            "train" => Ok(Self::Train),
            "validation" => Ok(Self::Validation),
            "test" => Ok(Self::Test),
            _ => bail!("unsupported effective split in prepared manifest: {value:?}"),
        }
    }

    fn priority(self) -> u8 {
        match self {
            Self::Train => 0,
            Self::Validation => 1,
            Self::Test => 2,
        }
    }
}

// =============================================================================
// TRAINING CLIP
// =============================================================================

/// Language-independent representation of one source training clip.
///
/// This is intentionally metadata-oriented.
///
/// Audio decoding is performed later by `audio/`.
#[derive(Debug, Clone)]
pub struct TrainingClip {
    pub language: LanguageId,
    pub dataset: DatasetKind,

    /// Absolute/relative resolved path to the actual source audio file.
    pub audio_path: PathBuf,

    /// Metadata-provided duration; measured output duration is stored separately.
    pub duration_ms: u64,

    /// Speaker/client identifier, when supplied by the corpus.
    pub client_id: Option<String>,

    /// Original split label from the source metadata or split filename.
    pub source_split: Option<String>,

    /// Effective split assigned to the complete client group.
    pub split: DataSplit,

    /// Original transcript text. Do not perform filler removal or normalization here.
    pub transcript: Option<String>,

    /// Source prompt/question when supplied by SPS.
    pub prompt: Option<String>,
}

// =============================================================================
// DATASET SELECTION
// =============================================================================

/// Selected clips after applying the per-language duration budget.
#[derive(Debug, Clone)]
pub struct DatasetSelection {
    clips: BTreeMap<LanguageId, Vec<TrainingClip>>,
    target_duration_ms: u64,
}

impl DatasetSelection {
    pub fn new(clips: BTreeMap<LanguageId, Vec<TrainingClip>>, target_duration_ms: u64) -> Self {
        Self {
            clips,
            target_duration_ms,
        }
    }

    /// Number of languages represented in the selection.
    pub fn languages(&self) -> usize {
        self.clips.len()
    }

    /// Configured target duration in milliseconds.
    pub fn target_duration_ms(&self) -> u64 {
        self.target_duration_ms
    }

    /// Get clips for one language.
    pub fn clips_for(&self, language: LanguageId) -> &[TrainingClip] {
        self.clips.get(&language).map(Vec::as_slice).unwrap_or(&[])
    }

    /// Iterate over selected languages in deterministic order.
    pub fn iter(&self) -> impl Iterator<Item = (&LanguageId, &Vec<TrainingClip>)> {
        self.clips.iter()
    }

    /// Total number of selected clips.
    pub fn total_clips(&self) -> usize {
        self.clips.values().map(Vec::len).sum()
    }

    /// Total selected duration for a language.
    pub fn duration_ms(&self, language: LanguageId) -> u64 {
        self.clips_for(language)
            .iter()
            .map(|clip| clip.duration_ms)
            .sum()
    }
}

// =============================================================================
// PREPARED DATASET
// =============================================================================

/// One prepared WAV file discovered under `offline_builder/data/`.
#[derive(Debug, Clone)]
pub struct PreparedClip {
    pub sample_id: String,
    pub language: LanguageId,
    pub dataset: DatasetKind,
    pub audio_path: PathBuf,
    pub sample_count: u64,
    pub duration_ms: u64,
    pub source_duration_ms: u64,
    pub client_id: Option<String>,
    pub source_split: Option<String>,
    pub split: DataSplit,
    pub transcript: Option<String>,
    pub prompt: Option<String>,
}

/// Versioned JSONL record describing one prepared FP32 audio file.
///
/// `audio_path` is relative to the prepared-data root so the dataset can be moved
/// without rewriting absolute paths. Transcript strings are preserved verbatim.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PreparedManifestRecord {
    pub schema_version: u32,
    pub sample_id: String,
    pub language: String,
    pub dataset: String,
    pub audio_path: String,
    pub sample_rate_hz: u32,
    pub channels: u16,
    pub sample_format: String,
    pub sample_count: u64,
    pub duration_ms: u64,
    pub source_duration_ms: u64,
    pub source_audio_filename: String,
    pub client_id: Option<String>,
    pub source_split: Option<String>,
    pub split: String,
    pub transcript: Option<String>,
    pub prompt: Option<String>,
}

pub const PREPARED_MANIFEST_SCHEMA_VERSION: u32 = 1;
pub const CANONICAL_SAMPLE_RATE_HZ: u32 = 16_000;
pub const CANONICAL_CHANNELS: u16 = 1;
pub const CANONICAL_FEATURE_DIMENSION: usize = 32;

/// Prepared dataset loaded from `offline_builder/data/`.
#[derive(Debug, Clone)]
pub struct PreparedDataset {
    clips: BTreeMap<LanguageId, Vec<PreparedClip>>,
}

impl PreparedDataset {
    pub fn languages(&self) -> usize {
        self.clips.len()
    }

    pub fn clips_for(&self, language: LanguageId) -> &[PreparedClip] {
        self.clips.get(&language).map(Vec::as_slice).unwrap_or(&[])
    }

    pub fn iter(&self) -> impl Iterator<Item = (&LanguageId, &Vec<PreparedClip>)> {
        self.clips.iter()
    }

    /// Iterate only the clips assigned to one split and language.
    ///
    /// This is the preferred API for training code: fit model parameters on
    /// `Train` clips, tune on `Validation`, and keep `Test` out of fitting.
    pub fn clips_for_split(
        &self,
        language: LanguageId,
        split: DataSplit,
    ) -> impl Iterator<Item = &PreparedClip> + '_ {
        self.clips_for(language)
            .iter()
            .filter(move |clip| clip.split == split)
    }

    /// Iterate through a split across all enabled languages without cloning
    /// sample metadata or audio paths.
    pub fn iter_split(
        &self,
        split: DataSplit,
    ) -> impl Iterator<Item = (LanguageId, &PreparedClip)> + '_ {
        self.clips.iter().flat_map(move |(language, clips)| {
            clips
                .iter()
                .filter(move |clip| clip.split == split)
                .map(move |clip| (*language, clip))
        })
    }

    pub fn total_clips(&self) -> usize {
        self.clips.values().map(Vec::len).sum()
    }
}

// =============================================================================
// GENERIC FILE DISCOVERY
// =============================================================================

/// Recursively discover files with a specific extension.
///
/// Results are sorted by path so discovery order is deterministic.
///
/// This is the core path-resolution primitive used by the dataset resolvers.
pub(crate) fn sorted_files_with_extension(root: &Path, extension: &str) -> Result<Vec<PathBuf>> {
    if !root.is_dir() {
        bail!(
            "dataset search root does not exist or is not a directory: {}",
            root.display()
        );
    }

    let extension = extension.trim_start_matches('.').to_ascii_lowercase();

    let mut files = Vec::new();

    for entry in WalkDir::new(root)
        .follow_links(false)
        .into_iter()
        .filter_map(std::result::Result::ok)
    {
        if !entry.file_type().is_file() {
            continue;
        }

        let path = entry.path();

        let matches = path
            .extension()
            .and_then(|ext| ext.to_str())
            .map(|ext| ext.eq_ignore_ascii_case(&extension))
            .unwrap_or(false);

        if matches {
            files.push(path.to_path_buf());
        }
    }

    files.sort();

    Ok(files)
}

// =============================================================================
// AUDIO INDEX
// =============================================================================

/// Build a unique basename -> path index.
///
/// We intentionally reject duplicate basenames rather than silently choosing
/// one. Silently choosing the wrong recording would corrupt the training set.
pub(crate) fn build_unique_audio_index(files: &[PathBuf]) -> Result<HashMap<String, PathBuf>> {
    // Explicit type annotation is required here because `with_capacity()`
    // alone does not provide enough information for Rust to infer K and V
    // before the later `existing.display()` usage.
    let mut index: HashMap<String, PathBuf> = HashMap::with_capacity(files.len());

    for path in files {
        let filename = path
            .file_name()
            .and_then(|name| name.to_str())
            .map(str::to_owned)
            .with_context(|| {
                format!("audio path has no valid UTF-8 filename: {}", path.display())
            })?;

        let key = filename.to_ascii_lowercase();

        if let Some(existing) = index.get(&key) {
            bail!(
                "duplicate audio filename detected:\n  {}\n  {}",
                existing.display(),
                path.display()
            );
        }

        index.insert(key, path.clone());
    }

    Ok(index)
}

// =============================================================================
// DATASET LAYOUT CHECK
// =============================================================================

/// Discover the three enabled language datasets.
///
/// No release date or version-specific directory name is required.
pub fn check_layout(raw_dir: &Path) -> Result<()> {
    ensure_directory(raw_dir, "raw dataset root")?;

    info!(
        root = %raw_dir.display(),
        "searching raw dataset tree"
    );

    let mut failures = Vec::new();

    // -------------------------------------------------------------------------
    // SPS
    // -------------------------------------------------------------------------

    for language in [LanguageId::En, LanguageId::Fr] {
        match sps::discover(raw_dir, language) {
            Ok(dataset) => {
                info!(
                    language = %language,
                    metadata = %dataset.metadata_tsv.display(),
                    mp3_files = dataset.audio_files.len(),
                    "SPS dataset discovered"
                );
            }

            Err(error) => {
                failures.push(format!("{} SPS: {}", language, error));
            }
        }
    }

    // -------------------------------------------------------------------------
    // Hindi Common Voice
    // -------------------------------------------------------------------------

    match common_voice::discover_hindi(raw_dir) {
        Ok(dataset) => {
            info!(
                metadata = %dataset.validated_tsv.display(),
                durations = %dataset.durations_tsv.display(),
                mp3_files = dataset.audio_files.len(),
                "Hindi Common Voice dataset discovered"
            );
        }

        Err(error) => {
            failures.push(format!("hi Common Voice: {}", error));
        }
    }

    if !failures.is_empty() {
        bail!(
            "dataset layout validation failed:\n{}",
            failures
                .into_iter()
                .map(|item| format!("  - {item}"))
                .collect::<Vec<_>>()
                .join("\n")
        );
    }

    Ok(())
}

// =============================================================================
// TRAINING CLIP SELECTION
// =============================================================================

/// Discover English/French SPS and Hindi Common Voice, then select up to the
/// configured duration budget per language. The budget is reserved 80/10/10
/// across train/validation/test so source row order cannot consume the entire
/// allowance with held-out data.
pub fn select_training_clips(raw_dir: &Path, hours_per_language: f64) -> Result<DatasetSelection> {
    anyhow::ensure!(
        hours_per_language.is_finite() && hours_per_language > 0.0,
        "hours_per_language must be finite and greater than zero"
    );

    let target_duration_ms = (hours_per_language * 3_600_000.0).round() as u64;
    anyhow::ensure!(
        target_duration_ms > 0,
        "duration budget rounds to zero milliseconds"
    );

    let mut selected = BTreeMap::new();

    let mut en = sps::load(&sps::discover(raw_dir, LanguageId::En)?, LanguageId::En)?;
    assign_effective_splits(&mut en);
    selected.insert(
        LanguageId::En,
        select_with_duration_limit(en, target_duration_ms),
    );

    let mut fr = sps::load(&sps::discover(raw_dir, LanguageId::Fr)?, LanguageId::Fr)?;
    assign_effective_splits(&mut fr);
    selected.insert(
        LanguageId::Fr,
        select_with_duration_limit(fr, target_duration_ms),
    );

    let mut hi = common_voice::load_hindi(&common_voice::discover_hindi(raw_dir)?)?;
    assign_effective_splits(&mut hi);
    selected.insert(
        LanguageId::Hi,
        select_with_duration_limit(hi, target_duration_ms),
    );

    let result = DatasetSelection::new(selected, target_duration_ms);

    for language in LanguageId::ALL {
        let clips = result.clips_for(language);
        if clips.is_empty() {
            warn!(language = %language, "no training clips selected");
            continue;
        }

        let duration_ms = result.duration_ms(language);
        info!(
            language = %language,
            clips = clips.len(),
            duration_ms,
            hours = duration_ms as f64 / 3_600_000.0,
            train_clips = clips.iter().filter(|clip| clip.split == DataSplit::Train).count(),
            validation_clips = clips.iter().filter(|clip| clip.split == DataSplit::Validation).count(),
            test_clips = clips.iter().filter(|clip| clip.split == DataSplit::Test).count(),
            "training clips selected"
        );

        if duration_ms < target_duration_ms {
            warn!(
                language = %language,
                selected_hours = duration_ms as f64 / 3_600_000.0,
                target_hours = hours_per_language,
                "dataset or split budgets exhausted before duration target"
            );
        }
    }

    Ok(result)
}

/// Select clips without ever exceeding the requested duration budget.
///
/// We preserve metadata order for deterministic source selection. We skip a
/// clip that would cross the boundary and continue looking for smaller clips.
fn select_with_duration_limit(
    clips: Vec<TrainingClip>,
    target_duration_ms: u64,
) -> Vec<TrainingClip> {
    // Reserve explicit holdout capacity rather than letting metadata file order
    // accidentally fill the complete budget with (for example) test rows.
    let train_budget = target_duration_ms.saturating_mul(80) / 100;
    let validation_budget = target_duration_ms.saturating_mul(10) / 100;
    let test_budget = target_duration_ms
        .saturating_sub(train_budget)
        .saturating_sub(validation_budget);

    let budgets = [
        (DataSplit::Train, train_budget),
        (DataSplit::Validation, validation_budget),
        (DataSplit::Test, test_budget),
    ];
    let mut by_split: BTreeMap<DataSplit, Vec<TrainingClip>> = BTreeMap::new();
    for clip in clips {
        by_split.entry(clip.split).or_default().push(clip);
    }

    let mut selected = Vec::new();
    for (split, budget_ms) in budgets {
        if budget_ms == 0 {
            continue;
        }
        let mut used_ms = 0_u64;
        if let Some(split_clips) = by_split.remove(&split) {
            for clip in split_clips {
                if used_ms >= budget_ms {
                    break;
                }
                let remaining_ms = budget_ms - used_ms;
                if clip.duration_ms > remaining_ms {
                    continue;
                }
                used_ms += clip.duration_ms;
                selected.push(clip);
            }
        }
    }

    selected
}

/// Assign one effective split per speaker/client group.
///
/// Recognized source split labels take precedence. If a source has inconsistent
/// labels for the same client, the more conservative holdout wins (test over
/// validation over train), with a warning. Missing/unknown labels use a stable
/// FNV-1a hash of the client ID to get a reproducible 80/10/10 split.
fn assign_effective_splits(clips: &mut [TrainingClip]) {
    let mut group_splits: BTreeMap<String, DataSplit> = BTreeMap::new();
    let mut conflicting_groups: BTreeSet<String> = BTreeSet::new();

    for clip in clips.iter() {
        let group_key = client_group_key(clip);
        if conflicting_groups.contains(&group_key) {
            continue;
        }
        let Some(source_split) = clip.source_split.as_deref().and_then(parse_source_split) else {
            continue;
        };

        match group_splits.get(&group_key).copied() {
            Some(existing) if existing != source_split => {
                // Do not force an entire speaker group into test merely because
                // source rows disagree. Keep all source labels in the manifest,
                // but assign this conflicting group using the stable hash below.
                warn!(
                    language = %clip.language,
                    group = %group_key,
                    first_split = existing.as_str(),
                    conflicting_split = source_split.as_str(),
                    "source split labels conflict for one client; using deterministic client-group split"
                );
                group_splits.remove(&group_key);
                conflicting_groups.insert(group_key);
            }
            Some(_) => {}
            None => {
                group_splits.insert(group_key, source_split);
            }
        }
    }

    for clip in clips.iter_mut() {
        let group_key = client_group_key(clip);
        let split = group_splits.get(&group_key).copied().unwrap_or_else(|| {
            let bucket = stable_hash64(group_key.as_bytes()) % 100;
            match bucket {
                0..=79 => DataSplit::Train,
                80..=89 => DataSplit::Validation,
                _ => DataSplit::Test,
            }
        });
        clip.split = split;
    }
}

fn client_group_key(clip: &TrainingClip) -> String {
    if let Some(client_id) = clip
        .client_id
        .as_deref()
        .filter(|value| !value.trim().is_empty())
    {
        return format!(
            "{}|{}|client:{}",
            clip.language.as_str(),
            clip.dataset.as_str(),
            client_id
        );
    }
    let filename = clip
        .audio_path
        .file_name()
        .and_then(|value| value.to_str())
        .unwrap_or("unknown-audio");
    format!(
        "{}|{}|audio:{}",
        clip.language.as_str(),
        clip.dataset.as_str(),
        filename
    )
}

fn parse_source_split(value: &str) -> Option<DataSplit> {
    match value.trim().to_ascii_lowercase().as_str() {
        "train" | "training" => Some(DataSplit::Train),
        "dev" | "val" | "valid" | "validation" | "development" => Some(DataSplit::Validation),
        "test" | "testing" => Some(DataSplit::Test),
        _ => None,
    }
}

fn stable_hash64(bytes: &[u8]) -> u64 {
    // FNV-1a is used instead of DefaultHasher because the split must remain
    // reproducible across processes and compiler versions.
    let mut hash = 0xcbf2_9ce4_8422_2325_u64;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    hash
}

// =============================================================================
// PREPARED WAV DATASET DISCOVERY
// =============================================================================

/// Load the manifest-backed prepared dataset from `data/manifest.jsonl`.
///
/// We intentionally do not recursively scan for WAV files: stale artifacts from
/// earlier preparation runs must never silently become training samples.
pub fn load_prepared_dataset(data_dir: &Path) -> Result<PreparedDataset> {
    ensure_directory(data_dir, "prepared data root")?;

    let manifest_path = data_dir.join("manifest.jsonl");
    let file = File::open(&manifest_path).with_context(|| {
        format!(
            "prepared manifest not found at {}; run `cargo run -p offline_builder -- prepare` to regenerate prepared FP32 WAV files and their transcript manifest",
            manifest_path.display()
        )
    })?;

    let mut by_language: BTreeMap<LanguageId, Vec<PreparedClip>> = BTreeMap::new();
    let mut seen_ids = HashSet::new();
    let mut line_count = 0_u64;

    for (zero_based_line, line_result) in BufReader::new(file).lines().enumerate() {
        let line_number = zero_based_line + 1;
        let line = line_result.with_context(|| {
            format!(
                "failed to read {} line {}",
                manifest_path.display(),
                line_number
            )
        })?;
        if line.trim().is_empty() {
            continue;
        }
        line_count += 1;

        let record: PreparedManifestRecord = serde_json::from_str(&line).with_context(|| {
            format!(
                "invalid JSONL record at {}:{}",
                manifest_path.display(),
                line_number
            )
        })?;

        anyhow::ensure!(
            record.schema_version == PREPARED_MANIFEST_SCHEMA_VERSION,
            "unsupported prepared manifest schema {} at line {}; expected {}",
            record.schema_version,
            line_number,
            PREPARED_MANIFEST_SCHEMA_VERSION
        );
        anyhow::ensure!(
            !record.sample_id.trim().is_empty(),
            "empty sample_id at manifest line {line_number}"
        );
        anyhow::ensure!(
            seen_ids.insert(record.sample_id.clone()),
            "duplicate sample_id {:?} at manifest line {line_number}",
            record.sample_id
        );
        anyhow::ensure!(
            record.sample_rate_hz == CANONICAL_SAMPLE_RATE_HZ,
            "sample {} has sample rate {} Hz; expected {} Hz",
            record.sample_id,
            record.sample_rate_hz,
            CANONICAL_SAMPLE_RATE_HZ
        );
        anyhow::ensure!(
            record.channels == CANONICAL_CHANNELS,
            "sample {} has {} channels; expected mono",
            record.sample_id,
            record.channels
        );
        anyhow::ensure!(
            record.sample_format == "ieee-float32",
            "sample {} uses format {:?}; expected ieee-float32",
            record.sample_id,
            record.sample_format
        );

        let language = LanguageId::parse(&record.language)?;
        let dataset = DatasetKind::parse(&record.dataset)?;
        let split = DataSplit::parse(&record.split)?;
        let relative_path = Path::new(&record.audio_path);
        anyhow::ensure!(
            !relative_path.is_absolute()
                && relative_path
                    .components()
                    .all(|component| matches!(component, Component::Normal(_) | Component::CurDir)),
            "manifest audio path must be relative and cannot escape the prepared-data root: {:?}",
            record.audio_path
        );
        anyhow::ensure!(
            relative_path
                .components()
                .next()
                .and_then(|component| component.as_os_str().to_str())
                == Some(language.as_str()),
            "sample {} audio path must be rooted in the language directory {:?}: {:?}",
            record.sample_id,
            language.as_str(),
            record.audio_path
        );
        anyhow::ensure!(
            relative_path
                .extension()
                .and_then(|extension| extension.to_str())
                .is_some_and(|extension| extension.eq_ignore_ascii_case("wav")),
            "sample {} audio path must point to a WAV file: {:?}",
            record.sample_id,
            record.audio_path
        );
        let audio_path = data_dir.join(relative_path);
        let (sample_count, duration_ms) =
            validate_prepared_f32_wav(&audio_path, record.sample_rate_hz).with_context(|| {
                format!("invalid prepared FP32 WAV for sample {}", record.sample_id)
            })?;
        anyhow::ensure!(
            sample_count == record.sample_count,
            "sample {} manifest sample count {} does not match WAV sample count {}",
            record.sample_id,
            record.sample_count,
            sample_count
        );
        anyhow::ensure!(
            duration_ms == record.duration_ms,
            "sample {} manifest duration {} ms does not match WAV duration {} ms",
            record.sample_id,
            record.duration_ms,
            duration_ms
        );

        by_language.entry(language).or_default().push(PreparedClip {
            sample_id: record.sample_id,
            language,
            dataset,
            audio_path,
            sample_count,
            duration_ms,
            source_duration_ms: record.source_duration_ms,
            client_id: record.client_id,
            source_split: record.source_split,
            split,
            transcript: record.transcript,
            prompt: record.prompt,
        });
    }

    anyhow::ensure!(
        line_count > 0 && !by_language.is_empty(),
        "prepared manifest {} contains no samples",
        manifest_path.display()
    );

    for clips in by_language.values_mut() {
        clips.sort_by(|left, right| left.sample_id.cmp(&right.sample_id));
    }

    info!(
        manifest = %manifest_path.display(),
        records = line_count,
        languages = by_language.len(),
        total_clips = by_language.values().map(Vec::len).sum::<usize>(),
        "loaded prepared dataset manifest"
    );

    Ok(PreparedDataset { clips: by_language })
}

fn validate_prepared_f32_wav(path: &Path, expected_sample_rate: u32) -> Result<(u64, u64)> {
    let reader = WavReader::open(path)
        .with_context(|| format!("failed to open prepared WAV: {}", path.display()))?;
    let spec = reader.spec();

    anyhow::ensure!(
        spec.sample_rate == expected_sample_rate,
        "{}: expected {} Hz, got {} Hz",
        path.display(),
        expected_sample_rate,
        spec.sample_rate
    );
    anyhow::ensure!(
        spec.channels == CANONICAL_CHANNELS,
        "{}: expected mono audio, got {} channels",
        path.display(),
        spec.channels
    );
    anyhow::ensure!(
        spec.bits_per_sample == 32,
        "{}: expected 32-bit float WAV, got {} bits",
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

// =============================================================================
// HELPERS
// =============================================================================

fn ensure_directory(path: &Path, description: &str) -> Result<()> {
    anyhow::ensure!(
        path.is_dir(),
        "{} does not exist or is not a directory: {}",
        description,
        path.display()
    );

    Ok(())
}

#[cfg(test)]
mod split_tests {
    use super::{parse_source_split, stable_hash64, DataSplit};

    #[test]
    fn source_split_aliases_are_normalized() {
        assert_eq!(parse_source_split("train"), Some(DataSplit::Train));
        assert_eq!(parse_source_split("dev"), Some(DataSplit::Validation));
        assert_eq!(
            parse_source_split("validation"),
            Some(DataSplit::Validation)
        );
        assert_eq!(parse_source_split("test"), Some(DataSplit::Test));
        assert_eq!(parse_source_split("unknown"), None);
    }

    #[test]
    fn stable_hash_is_reproducible() {
        assert_eq!(stable_hash64(b"client-1"), stable_hash64(b"client-1"));
        assert_ne!(stable_hash64(b"client-1"), stable_hash64(b"client-2"));
    }

    #[test]
    fn conflicting_source_splits_fall_back_to_one_stable_client_split() {
        use super::{assign_effective_splits, DataSplit, DatasetKind, LanguageId, TrainingClip};
        use std::path::PathBuf;

        let make_clip = |split: Option<&str>, name: &str| TrainingClip {
            language: LanguageId::En,
            dataset: DatasetKind::Sps,
            audio_path: PathBuf::from(name),
            duration_ms: 1_000,
            client_id: Some("same-client".to_owned()),
            source_split: split.map(str::to_owned),
            split: DataSplit::Train,
            transcript: Some("spoken words".to_owned()),
            prompt: None,
        };
        let mut clips = vec![
            make_clip(Some("train"), "one.mp3"),
            make_clip(Some("test"), "two.mp3"),
            make_clip(None, "three.mp3"),
        ];

        assign_effective_splits(&mut clips);
        assert!(clips.iter().all(|clip| clip.split == clips[0].split));
    }
}
