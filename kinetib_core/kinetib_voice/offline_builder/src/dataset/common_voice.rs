//! Hindi Common Voice resolver and loader.
//!
//! Preferred input is the source-provided train/dev/test metadata, preserving
//! those official partitions. `validated.tsv` is used only as a fallback when
//! the three split files are unavailable; in that case the caller assigns a
//! deterministic speaker-grouped split from `client_id`.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use csv::{ReaderBuilder, StringRecord};
use tracing::{info, warn};

use super::{
    build_unique_audio_index, sorted_files_with_extension, DataSplit, DatasetKind, LanguageId,
    TrainingClip,
};

#[derive(Debug, Clone)]
pub struct MetadataSplitFile {
    pub path: PathBuf,
    /// Source split label inferred from the official metadata filename.
    pub source_split: Option<String>,
}

#[derive(Debug, Clone)]
pub struct HindiDataset {
    pub validated_tsv: PathBuf,
    pub metadata_files: Vec<MetadataSplitFile>,
    pub durations_tsv: PathBuf,
    pub audio_files: Vec<PathBuf>,
}

/// Discover the Hindi Common Voice dataset without hard-coding a release name.
pub fn discover_hindi(raw_dir: &Path) -> Result<HindiDataset> {
    let tsv_files = sorted_files_with_extension(raw_dir, "tsv")?;
    let mut validated_candidates = Vec::new();

    for path in &tsv_files {
        let has_hi_component = path
            .components()
            .any(|component| component.as_os_str() == "hi");
        let is_validated = path
            .file_name()
            .and_then(|value| value.to_str())
            .is_some_and(|name| name.eq_ignore_ascii_case("validated.tsv"));
        if has_hi_component && is_validated {
            validated_candidates.push(path.clone());
        }
    }

    if validated_candidates.is_empty() {
        bail!(
            "could not find Hindi `validated.tsv` below {}",
            raw_dir.display()
        );
    }

    for validated_tsv in validated_candidates {
        let dataset_root = validated_tsv.parent().unwrap_or(raw_dir);
        let local_tsvs = sorted_files_with_extension(dataset_root, "tsv")?;

        let durations_tsv = local_tsvs
            .iter()
            .find(|path| {
                path.file_name()
                    .and_then(|value| value.to_str())
                    .is_some_and(|name| name.eq_ignore_ascii_case("clip_durations.tsv"))
            })
            .cloned();
        let Some(durations_tsv) = durations_tsv else {
            continue;
        };

        let audio_files = sorted_files_with_extension(dataset_root, "mp3")?;
        if audio_files.is_empty() {
            continue;
        }

        let mut official_splits = Vec::new();
        for (filename, label) in [
            ("train.tsv", "train"),
            ("dev.tsv", "dev"),
            ("test.tsv", "test"),
        ] {
            if let Some(path) = local_tsvs.iter().find(|path| {
                path.file_name()
                    .and_then(|value| value.to_str())
                    .is_some_and(|name| name.eq_ignore_ascii_case(filename))
            }) {
                official_splits.push(MetadataSplitFile {
                    path: path.clone(),
                    source_split: Some(label.to_owned()),
                });
            }
        }

        let metadata_files = if official_splits.len() == 3 {
            info!("using official Hindi Common Voice train/dev/test TSV files");
            official_splits
        } else {
            if !official_splits.is_empty() {
                warn!(
                    found_split_files = official_splits.len(),
                    "Hindi Common Voice split metadata is incomplete; falling back to validated.tsv and deterministic client-based splitting"
                );
            } else {
                info!("official Hindi split TSVs not found; using validated.tsv fallback");
            }
            vec![MetadataSplitFile {
                path: validated_tsv.clone(),
                source_split: None,
            }]
        };

        info!(
            validated = %validated_tsv.display(),
            durations = %durations_tsv.display(),
            metadata_files = metadata_files.len(),
            mp3_files = audio_files.len(),
            "resolved Hindi Common Voice dataset"
        );

        return Ok(HindiDataset {
            validated_tsv,
            metadata_files,
            durations_tsv,
            audio_files,
        });
    }

    bail!("found Hindi `validated.tsv` but could not resolve matching `clip_durations.tsv` and MP3 files")
}

/// Load audio/text pairs and keep their source split and speaker metadata.
pub fn load_hindi(dataset: &HindiDataset) -> Result<Vec<TrainingClip>> {
    let audio_index = build_unique_audio_index(&dataset.audio_files)?;
    let durations = load_duration_table(&dataset.durations_tsv)?;

    let mut clips = Vec::new();
    let mut seen_audio: HashMap<String, Option<String>> = HashMap::new();
    let mut total_rows = 0_u64;
    let mut missing_audio = 0_u64;
    let mut missing_duration = 0_u64;
    let mut invalid_duration = 0_u64;
    let mut duplicate_rows = 0_u64;

    for metadata_file in &dataset.metadata_files {
        let mut reader = ReaderBuilder::new()
            .delimiter(b'\t')
            .has_headers(true)
            .flexible(true)
            .from_path(&metadata_file.path)
            .with_context(|| {
                format!(
                    "failed to open Hindi Common Voice TSV: {}",
                    metadata_file.path.display()
                )
            })?;

        let headers = reader
            .headers()
            .context("failed to read Hindi TSV headers")?
            .clone();
        let path_idx = required_column(&headers, "path")?;
        let transcript_idx = required_column(&headers, "sentence")?;
        let client_idx = optional_column(&headers, "client_id");

        for record_result in reader.records() {
            let record = record_result.context("failed to read Hindi Common Voice TSV row")?;
            total_rows += 1;
            let clip_name = field(&record, path_idx).trim();
            if clip_name.is_empty() {
                continue;
            }
            let Some(filename) = Path::new(clip_name)
                .file_name()
                .and_then(|name| name.to_str())
            else {
                continue;
            };
            let key = filename.to_ascii_lowercase();

            let source_split = metadata_file.source_split.clone();
            if let Some(previous_split) = seen_audio.get(&key) {
                if previous_split != &source_split {
                    bail!(
                        "Hindi Common Voice audio {:?} appears in multiple source splits ({:?} and {:?}); fix the split metadata before training",
                        filename,
                        previous_split,
                        source_split
                    );
                }
                duplicate_rows += 1;
                continue;
            }
            seen_audio.insert(key.clone(), source_split.clone());

            let Some(audio_path) = audio_index.get(&key) else {
                missing_audio += 1;
                continue;
            };
            let Some(duration_ms) = durations.get(&key) else {
                missing_duration += 1;
                continue;
            };
            if *duration_ms == 0 {
                invalid_duration += 1;
                continue;
            }

            let transcript = optional_raw_field(&record, Some(transcript_idx));
            let client_id = optional_raw_field(&record, client_idx);

            clips.push(TrainingClip {
                language: LanguageId::Hi,
                dataset: DatasetKind::CommonVoice,
                audio_path: audio_path.clone(),
                duration_ms: *duration_ms,
                client_id,
                source_split,
                // Replaced by dataset::assign_effective_splits before selection.
                split: DataSplit::Train,
                transcript,
                prompt: None,
            });
        }
    }

    info!(
        rows = total_rows,
        usable_clips = clips.len(),
        missing_audio,
        missing_duration,
        invalid_duration,
        duplicate_rows,
        "loaded Hindi Common Voice metadata"
    );

    if clips.is_empty() {
        bail!("Hindi Common Voice metadata produced zero usable clips");
    }
    if missing_audio > 0 {
        warn!(
            missing_audio,
            "some Hindi Common Voice rows referenced missing MP3 files"
        );
    }
    if missing_duration > 0 {
        warn!(missing_duration, "some Hindi clips had no duration entry");
    }
    if invalid_duration > 0 {
        warn!(
            invalid_duration,
            "some Hindi clips had invalid duration metadata"
        );
    }
    if duplicate_rows > 0 {
        warn!(duplicate_rows, "duplicate Hindi audio records were ignored");
    }

    Ok(clips)
}

fn load_duration_table(path: &Path) -> Result<HashMap<String, u64>> {
    let mut reader = ReaderBuilder::new()
        .delimiter(b'\t')
        .has_headers(true)
        .flexible(true)
        .from_path(path)
        .with_context(|| format!("failed to open clip_durations.tsv: {}", path.display()))?;
    let headers = reader
        .headers()
        .context("failed to read clip_durations.tsv headers")?
        .clone();
    let clip_idx = required_column(&headers, "clip")?;
    let duration_idx = required_column(&headers, "duration[ms]")?;
    let mut durations = HashMap::new();

    for record_result in reader.records() {
        let record = record_result.context("failed to read clip_durations.tsv row")?;
        let clip = field(&record, clip_idx).trim();
        let duration_text = field(&record, duration_idx).trim();
        if clip.is_empty() {
            continue;
        }
        let Some(filename) = Path::new(clip).file_name().and_then(|name| name.to_str()) else {
            continue;
        };
        let Ok(duration_ms) = duration_text.parse::<u64>() else {
            continue;
        };
        durations
            .entry(filename.to_ascii_lowercase())
            .or_insert(duration_ms);
    }

    anyhow::ensure!(
        !durations.is_empty(),
        "clip_durations.tsv contained no usable duration entries: {}",
        path.display()
    );
    info!(
        entries = durations.len(),
        "loaded Hindi clip duration table"
    );
    Ok(durations)
}

fn required_column(headers: &StringRecord, name: &str) -> Result<usize> {
    headers
        .iter()
        .position(|header| header.trim() == name)
        .with_context(|| format!("required Common Voice TSV column '{}' was not found", name))
}

fn optional_column(headers: &StringRecord, name: &str) -> Option<usize> {
    headers.iter().position(|header| header.trim() == name)
}

fn field<'a>(record: &'a StringRecord, index: usize) -> &'a str {
    record.get(index).unwrap_or("")
}

fn optional_raw_field(record: &StringRecord, index: Option<usize>) -> Option<String> {
    index
        .and_then(|index| record.get(index))
        .filter(|value| !value.trim().is_empty())
        .map(str::to_owned)
}
