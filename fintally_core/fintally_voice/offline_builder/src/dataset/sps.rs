//! SPS Corpus dataset resolver and loader.
//!
//! English and French use `ss-corpus-<language>.tsv`. Transcript text is kept
//! exactly as supplied (apart from treating all-whitespace values as absent).
//! Reported-audio metadata is consulted so a clip reported by the source corpus
//! cannot accidentally enter the selected training set if it is also listed in
//! the main corpus metadata.

use std::collections::HashSet;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use csv::{ReaderBuilder, StringRecord};
use tracing::{info, warn};

use super::{
    build_unique_audio_index, sorted_files_with_extension, DataSplit, DatasetKind, LanguageId,
    TrainingClip,
};

#[derive(Debug, Clone)]
pub struct SpsDataset {
    pub metadata_tsv: PathBuf,
    pub reported_metadata_tsv: Option<PathBuf>,
    pub audio_files: Vec<PathBuf>,
}

/// Recursively locate the SPS metadata TSV for English or French.
pub fn discover(raw_dir: &Path, language: LanguageId) -> Result<SpsDataset> {
    anyhow::ensure!(
        matches!(language, LanguageId::En | LanguageId::Fr),
        "{} is not enabled as an SPS language; supported SPS languages are en and fr",
        language
    );

    let expected_filename = format!("ss-corpus-{}.tsv", language.as_str());
    let tsv_files = sorted_files_with_extension(raw_dir, "tsv")?;
    let mut candidates = Vec::new();

    for path in tsv_files {
        if path
            .file_name()
            .and_then(|value| value.to_str())
            .is_some_and(|name| name.eq_ignore_ascii_case(&expected_filename))
        {
            candidates.push(path);
        }
    }

    if candidates.is_empty() {
        bail!(
            "could not find SPS metadata file '{}' below {}",
            expected_filename,
            raw_dir.display()
        );
    }

    for metadata_tsv in &candidates {
        let search_root = metadata_tsv.parent().unwrap_or(raw_dir);
        let audio_files = sorted_files_with_extension(search_root, "mp3")?;
        if audio_files.is_empty() {
            continue;
        }

        let reported_name = format!("ss-reported-audios-{}.tsv", language.as_str());
        let reported_metadata_tsv = search_root.join(reported_name);
        let reported_metadata_tsv = reported_metadata_tsv
            .is_file()
            .then_some(reported_metadata_tsv);

        info!(
            language = %language,
            metadata = %metadata_tsv.display(),
            mp3_files = audio_files.len(),
            has_reported_audio_metadata = reported_metadata_tsv.is_some(),
            "resolved SPS dataset"
        );

        return Ok(SpsDataset {
            metadata_tsv: metadata_tsv.clone(),
            reported_metadata_tsv,
            audio_files,
        });
    }

    bail!(
        "found '{}' but no MP3 files under its dataset tree",
        expected_filename
    )
}

/// Parse SPS metadata into language-independent clips.
pub fn load(dataset: &SpsDataset, language: LanguageId) -> Result<Vec<TrainingClip>> {
    let audio_index = build_unique_audio_index(&dataset.audio_files)?;
    let reported_audio = match dataset.reported_metadata_tsv.as_deref() {
        Some(path) => load_reported_audio_names(path)?,
        None => HashSet::new(),
    };

    let mut reader = ReaderBuilder::new()
        .delimiter(b'\t')
        .has_headers(true)
        .flexible(true)
        .from_path(&dataset.metadata_tsv)
        .with_context(|| {
            format!(
                "failed to open SPS metadata: {}",
                dataset.metadata_tsv.display()
            )
        })?;
    let headers = reader
        .headers()
        .context("failed to read SPS TSV headers")?
        .clone();

    let audio_file_idx = required_column(&headers, "audio_file")?;
    let duration_idx = required_column(&headers, "duration_ms")?;
    let transcript_idx = optional_column(&headers, "transcription");
    let client_idx = optional_column(&headers, "client_id");
    let prompt_idx = optional_column(&headers, "prompt");
    let split_idx = optional_column(&headers, "split");

    let mut clips = Vec::new();
    let mut rows = 0_u64;
    let mut missing_audio = 0_u64;
    let mut invalid_duration = 0_u64;
    let mut reported_rows_excluded = 0_u64;

    for record_result in reader.records() {
        let record = record_result.context("failed to read SPS TSV row")?;
        rows += 1;

        let audio_name = field(&record, audio_file_idx).trim();
        if audio_name.is_empty() {
            continue;
        }
        let Some(filename) = Path::new(audio_name)
            .file_name()
            .and_then(|name| name.to_str())
        else {
            continue;
        };
        let key = filename.to_ascii_lowercase();

        if reported_audio.contains(&key) {
            reported_rows_excluded += 1;
            continue;
        }

        let Some(audio_path) = audio_index.get(&key) else {
            missing_audio += 1;
            continue;
        };

        let duration_ms = match field(&record, duration_idx).trim().parse::<u64>() {
            Ok(value) if value > 0 => value,
            _ => {
                invalid_duration += 1;
                continue;
            }
        };

        let transcript = optional_raw_field(&record, transcript_idx);
        let client_id = optional_raw_field(&record, client_idx);
        let prompt = optional_raw_field(&record, prompt_idx);
        let source_split = optional_raw_field(&record, split_idx);

        clips.push(TrainingClip {
            language,
            dataset: DatasetKind::Sps,
            audio_path: audio_path.clone(),
            duration_ms,
            client_id,
            source_split,
            // Replaced by dataset::assign_effective_splits before selection.
            split: DataSplit::Train,
            transcript,
            prompt,
        });
    }

    info!(
        language = %language,
        rows,
        usable_clips = clips.len(),
        missing_audio,
        invalid_duration,
        reported_rows_excluded,
        "loaded SPS metadata"
    );

    if clips.is_empty() {
        bail!("SPS metadata produced zero usable clips for {}", language);
    }
    if missing_audio > 0 {
        warn!(language = %language, missing_audio, "some SPS metadata rows referenced missing MP3 files");
    }
    if invalid_duration > 0 {
        warn!(language = %language, invalid_duration, "some SPS rows contained invalid duration metadata");
    }

    Ok(clips)
}

/// Read the source's reported-audio table, if present, as a case-insensitive
/// filename set. Only exact filenames are matched; no fuzzy matching is done.
fn load_reported_audio_names(path: &Path) -> Result<HashSet<String>> {
    let mut reader = ReaderBuilder::new()
        .delimiter(b'\t')
        .has_headers(true)
        .flexible(true)
        .from_path(path)
        .with_context(|| {
            format!(
                "failed to open SPS reported-audio metadata: {}",
                path.display()
            )
        })?;
    let headers = reader
        .headers()
        .context("failed to read reported-audio TSV headers")?
        .clone();
    let audio_file_idx = required_column(&headers, "audio_file")?;
    let mut names = HashSet::new();

    for row in reader.records() {
        let record = row.context("failed to read reported-audio TSV row")?;
        let audio_name = record.get(audio_file_idx).unwrap_or("").trim();
        if audio_name.is_empty() {
            continue;
        }
        if let Some(filename) = Path::new(audio_name)
            .file_name()
            .and_then(|name| name.to_str())
        {
            names.insert(filename.to_ascii_lowercase());
        }
    }

    info!(path = %path.display(), reported_audio_files = names.len(), "loaded SPS reported-audio list");
    Ok(names)
}

fn required_column(headers: &StringRecord, name: &str) -> Result<usize> {
    headers
        .iter()
        .position(|header| header.trim() == name)
        .with_context(|| format!("required SPS TSV column '{}' was not found", name))
}

fn optional_column(headers: &StringRecord, name: &str) -> Option<usize> {
    headers.iter().position(|header| header.trim() == name)
}

fn field<'a>(record: &'a StringRecord, index: usize) -> &'a str {
    record.get(index).unwrap_or("")
}

/// Keep the original text, including meaningful leading/trailing whitespace;
/// use trim only to decide whether a field is empty.
fn optional_raw_field(record: &StringRecord, index: Option<usize>) -> Option<String> {
    index
        .and_then(|index| record.get(index))
        .filter(|value| !value.trim().is_empty())
        .map(str::to_owned)
}
