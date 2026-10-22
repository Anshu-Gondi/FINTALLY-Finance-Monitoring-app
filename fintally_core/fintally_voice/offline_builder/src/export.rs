//! FinTally Voice codebook export.
//!
//! The canonical model is a versioned, little-endian FP32 `.fvcb` artifact.
//! This module also emits:
//! - a human-readable JSON manifest into the requested output directory;
//! - generated C read-only tables under `c_core/include/codebooks` and
//!   `c_core/src`, suitable for firmware/`.rodata` deployment;
//! - the canonical artifact under `src/codebooks` plus a small Rust module
//!   (`src/fin_voice_codebooks.rs`) exposing it through `include_bytes!`.
//!
//! Binary layout, version 1 (all integers and floats are little-endian):
//!
//! Fixed 84-byte header:
//!   magic[8] = b"FVCB\r\n\x1a\n"
//!   format_version:u32, header_size:u32, payload_size:u64,
//!   payload_crc32:u32, codebook_schema_version:u32, feature_schema_version:u32,
//!   feature_dim:u32, sample_rate_hz:u32, channels:u32, frame_samples:u32,
//!   hop_samples:u32, normalization_schema_version:u32, training_frame_count:u64,
//!   kmeans_seed:u64, language_count:u32, reserved:u32
//!
//! Payload:
//!   normalization mean[feature_dim]:f32, inverse_std[feature_dim]:f32,
//!   followed by language entries sorted as en, fr, hi. Each entry contains
//!   language_id:u8 (1=en, 2=fr, 3=hi), backend_id:u8 (1=Rayon CPU, 2=CUDA),
//!   flags:u16 (bit 0 means converged; all other bits reserved), dimensions:u32,
//!   centroid_count:u32, iterations:u32, training_samples:u64, inertia:f64,
//!   and centroid_count * dimensions IEEE-754 FP32 coordinates. Payload CRC32
//!   uses the standard IEEE polynomial 0xEDB88320 and covers the full payload.
//!
//! `FEATURE_SCHEMA_VERSION` must be incremented whenever feature ordering,
//! dimensions, units, or feature interpretation changes.
//!
//! No Rust struct memory is serialized directly; padding and compiler layout
//! are therefore not part of the file contract.

use std::fmt::Write as FmtWrite;
use std::fs::{self, OpenOptions};
use std::io::Write as IoWrite;
use std::path::{Path, PathBuf};

use anyhow::{ensure, Context, Result};

use crate::dataset::LanguageId;
use crate::features::{self, FEATURE_DIMENSION};
use crate::kmeans::{Codebook, CodebookSet, ComputeBackend, CODEBOOK_SCHEMA_VERSION};

pub const CODEBOOK_FORMAT_VERSION: u32 = 1;
/// Bump when feature ordering, dimensions, units, or interpretation changes.
pub const FEATURE_SCHEMA_VERSION: u32 = 1;
const ARTIFACT_MAGIC: &[u8; 8] = b"FVCB\r\n\x1a\n";
const ARTIFACT_HEADER_SIZE: u32 = 84;
const CANONICAL_SAMPLE_RATE_HZ: u32 = crate::dataset::CANONICAL_SAMPLE_RATE_HZ;
const EXPECTED_LANGUAGE_COUNT: usize = 3;

const LANGUAGES: [LanguageId; EXPECTED_LANGUAGE_COUNT] =
    [LanguageId::En, LanguageId::Fr, LanguageId::Hi];

/// Export a trained codebook set to the canonical `.fvcb` artifact and to the
/// C/Rust integration paths in the FinTally Voice repository.
///
/// This signature intentionally matches `main.rs`:
/// `export::export_codebooks(&codebooks, &config.output_dir)`.
pub fn export_codebooks(codebooks: &CodebookSet, output_dir: &Path) -> Result<()> {
    validate_codebook_set(codebooks)?;

    fs::create_dir_all(output_dir).with_context(|| {
        format!(
            "failed to create codebook output directory: {}",
            output_dir.display()
        )
    })?;

    let project_root = project_root()?;
    let (artifact_bytes, payload_crc32) = encode_artifact(codebooks)?;

    let output_artifact = output_dir.join("fin_voice_codebooks.fvcb");
    let output_manifest = output_dir.join("fin_voice_codebooks.manifest.json");

    // Keep the versioned artifact as the canonical deliverable in output_dir.
    write_atomic(&output_artifact, &artifact_bytes)?;

    let c_include_dir = project_root.join("c_core/include/codebooks");
    let c_source_dir = project_root.join("c_core/src");
    let rust_codebook_dir = project_root.join("src/codebooks");

    fs::create_dir_all(&c_include_dir).with_context(|| {
        format!(
            "failed to create C codebook include directory: {}",
            c_include_dir.display()
        )
    })?;
    fs::create_dir_all(&c_source_dir).with_context(|| {
        format!(
            "failed to access C source directory: {}",
            c_source_dir.display()
        )
    })?;
    fs::create_dir_all(&rust_codebook_dir).with_context(|| {
        format!(
            "failed to create Rust codebook directory: {}",
            rust_codebook_dir.display()
        )
    })?;

    let header = render_c_header(codebooks)?;
    let c_source = render_c_source(codebooks)?;
    let manifest = render_manifest(codebooks, payload_crc32);
    let rust_module = render_rust_module(codebooks.schema_version);

    write_atomic(
        &c_include_dir.join("fin_voice_codebook_data.h"),
        header.as_bytes(),
    )?;
    write_atomic(
        &c_source_dir.join("generated_codebook_data.c"),
        c_source.as_bytes(),
    )?;
    write_atomic(
        &rust_codebook_dir.join("fin_voice_codebooks.fvcb"),
        &artifact_bytes,
    )?;
    write_atomic(
        &project_root.join("src/fin_voice_codebooks.rs"),
        rust_module.as_bytes(),
    )?;
    write_atomic(&output_manifest, manifest.as_bytes())?;

    tracing::info!(
        artifact = %output_artifact.display(),
        artifact_bytes = artifact_bytes.len(),
        checksum_crc32 = format!("{payload_crc32:08x}"),
        c_header = %c_include_dir.join("fin_voice_codebook_data.h").display(),
        c_source = %c_source_dir.join("generated_codebook_data.c").display(),
        rust_artifact = %rust_codebook_dir.join("fin_voice_codebooks.fvcb").display(),
        "FinTally Voice codebooks exported"
    );

    Ok(())
}

fn project_root() -> Result<PathBuf> {
    // CARGO_MANIFEST_DIR points at offline_builder/, so its parent is the
    // repository root containing sibling c_core/ and src/ directories. This
    // deliberately avoids dependence on the shell's current working directory.
    let manifest_dir = Path::new(env!("CARGO_MANIFEST_DIR"));
    let root = manifest_dir
        .parent()
        .context("offline_builder manifest directory has no parent repository")?;

    ensure!(
        root.join("c_core").is_dir(),
        "expected c_core/ alongside offline_builder at {}",
        root.display()
    );
    ensure!(
        root.join("src").is_dir(),
        "expected Rust runtime src/ alongside offline_builder at {}",
        root.display()
    );

    Ok(root.to_path_buf())
}

fn validate_codebook_set(set: &CodebookSet) -> Result<()> {
    ensure!(
        set.schema_version == CODEBOOK_SCHEMA_VERSION,
        "unsupported codebook schema version {}; expected {}",
        set.schema_version,
        CODEBOOK_SCHEMA_VERSION
    );
    ensure!(
        set.feature_dim == FEATURE_DIMENSION,
        "codebook feature dimension {} does not match native DSP dimension {}",
        set.feature_dim,
        FEATURE_DIMENSION
    );
    ensure!(
        set.feature_dim == features::FEATURE_DIMENSION,
        "feature dimension does not match the features module contract"
    );
    ensure!(
        set.normalization.schema_version > 0,
        "normalization schema version must be greater than zero"
    );
    ensure!(
        set.normalization.training_frame_count > 0,
        "normalization has no training frames"
    );
    ensure!(
        set.normalization.mean.iter().all(|value| value.is_finite()),
        "normalization mean contains a non-finite value"
    );
    ensure!(
        set.normalization
            .inv_std
            .iter()
            .all(|value| value.is_finite() && *value > 0.0),
        "normalization inverse standard deviation contains a non-finite or non-positive value"
    );
    ensure!(
        set.codebooks.len() == EXPECTED_LANGUAGE_COUNT,
        "expected exactly {} language codebooks, found {}",
        EXPECTED_LANGUAGE_COUNT,
        set.codebooks.len()
    );

    for language in LANGUAGES {
        let codebook = set
            .get(language)
            .with_context(|| format!("missing required {language} codebook"))?;
        validate_codebook(codebook, language)?;
    }

    for codebook in &set.codebooks {
        ensure!(
            LANGUAGES.contains(&codebook.language),
            "unexpected language codebook: {}",
            codebook.language
        );
    }

    Ok(())
}

fn validate_codebook(codebook: &Codebook, expected_language: LanguageId) -> Result<()> {
    ensure!(
        codebook.language == expected_language,
        "internal language mismatch: expected {expected_language}, got {}",
        codebook.language
    );
    ensure!(
        codebook.dimensions == FEATURE_DIMENSION,
        "{expected_language} codebook has {} dimensions; expected {}",
        codebook.dimensions,
        FEATURE_DIMENSION
    );
    ensure!(
        codebook.centroids.len() >= 2,
        "{expected_language} codebook must contain at least two centroids"
    );
    ensure!(
        codebook.centroids.len() <= u32::MAX as usize,
        "{expected_language} centroid count exceeds the artifact format limit"
    );
    ensure!(
        codebook.training_samples > 0,
        "{expected_language} codebook has no training samples"
    );
    ensure!(
        codebook.iterations > 0 && codebook.iterations <= u32::MAX as usize,
        "{expected_language} iteration count is invalid"
    );
    ensure!(
        codebook.inertia.is_finite() && codebook.inertia >= 0.0,
        "{expected_language} codebook inertia must be finite and non-negative"
    );
    ensure!(
        codebook
            .centroids
            .iter()
            .flatten()
            .all(|value| value.is_finite()),
        "{expected_language} centroid data contains a non-finite value"
    );
    ensure!(
        codebook
            .centroids
            .iter()
            .all(|centroid| centroid.len() == codebook.dimensions),
        "{expected_language} contains a centroid with an inconsistent dimension"
    );
    Ok(())
}

fn encode_artifact(set: &CodebookSet) -> Result<(Vec<u8>, u32)> {
    let mut payload = Vec::new();
    let normalization = &set.normalization;

    for value in normalization.mean.iter() {
        push_f32(&mut payload, *value);
    }
    for value in normalization.inv_std.iter() {
        push_f32(&mut payload, *value);
    }

    for language in LANGUAGES {
        let codebook = set
            .get(language)
            .with_context(|| format!("missing codebook for {language}"))?;
        let (language_id, _) = language_code(language);
        payload.push(language_id);
        payload.push(backend_code(codebook.backend));
        let flags: u16 = if codebook.converged { 1 } else { 0 };
        payload.extend_from_slice(&flags.to_le_bytes());
        push_u32(
            &mut payload,
            to_u32(codebook.dimensions, "feature dimension")?,
        );
        push_u32(
            &mut payload,
            to_u32(codebook.centroids.len(), "centroid count")?,
        );
        push_u32(
            &mut payload,
            to_u32(codebook.iterations, "iteration count")?,
        );
        payload.extend_from_slice(&codebook.training_samples.to_le_bytes());
        payload.extend_from_slice(&codebook.inertia.to_le_bytes());

        for centroid in &codebook.centroids {
            for value in centroid.iter() {
                push_f32(&mut payload, *value);
            }
        }
    }

    let payload_size = u64::try_from(payload.len()).context("codebook payload is too large")?;
    let payload_crc32 = crc32(&payload);

    let mut artifact = Vec::with_capacity(ARTIFACT_HEADER_SIZE as usize + payload.len());
    artifact.extend_from_slice(ARTIFACT_MAGIC);
    push_u32(&mut artifact, CODEBOOK_FORMAT_VERSION);
    push_u32(&mut artifact, ARTIFACT_HEADER_SIZE);
    artifact.extend_from_slice(&payload_size.to_le_bytes());
    push_u32(&mut artifact, payload_crc32);
    push_u32(&mut artifact, set.schema_version);
    push_u32(&mut artifact, FEATURE_SCHEMA_VERSION);
    push_u32(&mut artifact, to_u32(set.feature_dim, "feature dimension")?);
    push_u32(&mut artifact, CANONICAL_SAMPLE_RATE_HZ);
    push_u32(&mut artifact, u32::from(crate::dataset::CANONICAL_CHANNELS));
    push_u32(
        &mut artifact,
        to_u32(features::FRAME_SAMPLES, "frame sample count")?,
    );
    push_u32(
        &mut artifact,
        to_u32(features::HOP_SAMPLES, "hop sample count")?,
    );
    push_u32(&mut artifact, normalization.schema_version);
    artifact.extend_from_slice(&normalization.training_frame_count.to_le_bytes());
    artifact.extend_from_slice(&set.seed.to_le_bytes());
    push_u32(
        &mut artifact,
        to_u32(set.codebooks.len(), "language count")?,
    );
    push_u32(&mut artifact, 0); // Reserved; must be zero in format version 1.

    ensure!(
        artifact.len() == ARTIFACT_HEADER_SIZE as usize,
        "internal FVCB header size mismatch: generated {} bytes, expected {}",
        artifact.len(),
        ARTIFACT_HEADER_SIZE
    );
    artifact.extend_from_slice(&payload);
    Ok((artifact, payload_crc32))
}

fn render_c_header(set: &CodebookSet) -> Result<String> {
    let en_count = centroid_count(set, LanguageId::En)?;
    let fr_count = centroid_count(set, LanguageId::Fr)?;
    let hi_count = centroid_count(set, LanguageId::Hi)?;

    let mut out = String::new();
    out.push_str(
        "/* Generated by offline_builder/src/export.rs. Do not edit by hand. */\n\
         #ifndef FIN_VOICE_CODEBOOK_DATA_H\n\
         #define FIN_VOICE_CODEBOOK_DATA_H\n\
         #include <stdint.h>\n\
         #ifdef __cplusplus\n\
         extern \"C\" {\n\
         #endif\n\n",
    );
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_FORMAT_VERSION {}u",
        CODEBOOK_FORMAT_VERSION
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_SCHEMA_VERSION {}u",
        set.schema_version
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_FEATURE_SCHEMA_VERSION {}u",
        FEATURE_SCHEMA_VERSION
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_SAMPLE_RATE_HZ {}u",
        CANONICAL_SAMPLE_RATE_HZ
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_CHANNELS {}u",
        crate::dataset::CANONICAL_CHANNELS
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_FRAME_SAMPLES {}u",
        features::FRAME_SAMPLES
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_HOP_SAMPLES {}u",
        features::HOP_SAMPLES
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_NORMALIZATION_SCHEMA_VERSION {}u",
        set.normalization.schema_version
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_FEATURE_DIMENSION {}u",
        set.feature_dim
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_LANGUAGE_COUNT {}u",
        EXPECTED_LANGUAGE_COUNT
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_EN_CENTROID_COUNT {en_count}u"
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_FR_CENTROID_COUNT {fr_count}u"
    )?;
    writeln!(
        out,
        "#define FIN_VOICE_CODEBOOK_HI_CENTROID_COUNT {hi_count}u\n"
    )?;

    out.push_str(
        "typedef struct FinVoiceCodebookView {\n\
             const char *language;\n\
             const float *centroids;\n\
             uint32_t dimensions;\n\
             uint32_t centroid_count;\n\
             uint64_t training_samples;\n\
             uint32_t iterations;\n\
             uint32_t converged;\n\
             uint32_t backend; /* 1 = Rayon CPU, 2 = CUDA assignment. */\n\
             double inertia;\n\
         } FinVoiceCodebookView;\n\n",
    );

    out.push_str(
        "extern const float fin_voice_feature_normalization_mean[FIN_VOICE_FEATURE_DIMENSION];\n\
         extern const float fin_voice_feature_normalization_inv_std[FIN_VOICE_FEATURE_DIMENSION];\n\
         extern const float fin_voice_en_centroids[FIN_VOICE_CODEBOOK_EN_CENTROID_COUNT][FIN_VOICE_FEATURE_DIMENSION];\n\
         extern const float fin_voice_fr_centroids[FIN_VOICE_CODEBOOK_FR_CENTROID_COUNT][FIN_VOICE_FEATURE_DIMENSION];\n\
         extern const float fin_voice_hi_centroids[FIN_VOICE_CODEBOOK_HI_CENTROID_COUNT][FIN_VOICE_FEATURE_DIMENSION];\n\
         extern const FinVoiceCodebookView fin_voice_codebooks[FIN_VOICE_CODEBOOK_LANGUAGE_COUNT];\n\n\
         #ifdef __cplusplus\n\
         }\n\
         #endif\n\
         #endif /* FIN_VOICE_CODEBOOK_DATA_H */\n",
    );
    Ok(out)
}

fn render_c_source(set: &CodebookSet) -> Result<String> {
    let mut out = String::new();
    out.push_str(
        "/* Generated by offline_builder/src/export.rs. Do not edit by hand. */\n\
         #include <stdint.h>\n\
         #include \"codebooks/fin_voice_codebook_data.h\"\n\n",
    );

    out.push_str(
        "const float fin_voice_feature_normalization_mean[FIN_VOICE_FEATURE_DIMENSION] = {\n",
    );
    write_float_vector(&mut out, &set.normalization.mean, "    ")?;
    out.push_str("};\n\n");

    out.push_str(
        "const float fin_voice_feature_normalization_inv_std[FIN_VOICE_FEATURE_DIMENSION] = {\n",
    );
    write_float_vector(&mut out, &set.normalization.inv_std, "    ")?;
    out.push_str("};\n\n");

    for language in LANGUAGES {
        let codebook = set
            .get(language)
            .context("validated codebook disappeared")?;
        let symbol = centroid_symbol(language);
        let count_macro = centroid_count_macro(language);
        writeln!(
            out,
            "const float {symbol}[{count_macro}][FIN_VOICE_FEATURE_DIMENSION] = {{"
        )?;
        for centroid in &codebook.centroids {
            out.push_str("    { ");
            for (index, value) in centroid.iter().enumerate() {
                if index > 0 {
                    out.push_str(", ");
                }
                out.push_str(&format_c_f32(*value));
            }
            out.push_str(" },\n");
        }
        out.push_str("};\n\n");
    }

    out.push_str(
        "const FinVoiceCodebookView fin_voice_codebooks[FIN_VOICE_CODEBOOK_LANGUAGE_COUNT] = {\n",
    );
    for language in LANGUAGES {
        let codebook = set
            .get(language)
            .context("validated codebook disappeared")?;
        let (language_id, language_text) = language_code(language);
        let _ = language_id; // The binary format uses numeric IDs; C uses readable tags.
        writeln!(
            out,
            "    {{ \"{language_text}\", &{}[0][0], {}u, {}u, UINT64_C({}), {}u, {}u, {}u, {} }},",
            centroid_symbol(language),
            codebook.dimensions,
            codebook.centroids.len(),
            codebook.training_samples,
            codebook.iterations,
            if codebook.converged { 1 } else { 0 },
            backend_code(codebook.backend),
            format_c_f64(codebook.inertia)
        )?;
    }
    out.push_str("};\n");
    Ok(out)
}

fn write_float_vector(out: &mut String, values: &[f32], indent: &str) -> Result<()> {
    for (index, value) in values.iter().enumerate() {
        if index % 4 == 0 {
            out.push_str(indent);
        } else {
            out.push(' ');
        }
        write!(out, "{}", format_c_f32(*value))?;
        if index + 1 != values.len() {
            out.push_str(", ");
        }
        if index % 4 == 3 || index + 1 == values.len() {
            out.push('\n');
        }
    }
    Ok(())
}

fn render_rust_module(schema_version: u32) -> String {
    format!(
        "//! Generated by offline_builder/src/export.rs. Do not edit by hand.\n\
         //! Add `pub mod fin_voice_codebooks;` to `src/lib.rs` to expose this module.\n\n\
         pub const FIN_VOICE_CODEBOOK_FORMAT_VERSION: u32 = {CODEBOOK_FORMAT_VERSION};\n\
         pub const FIN_VOICE_CODEBOOK_SCHEMA_VERSION: u32 = {schema_version};\n\
         pub const FIN_VOICE_CODEBOOK_FEATURE_SCHEMA_VERSION: u32 = {FEATURE_SCHEMA_VERSION};\n\
         /// Canonical FP32 codebook artifact, embedded into the Rust runtime binary.\n\
         pub static FIN_VOICE_CODEBOOK_FVCB: &[u8] =\n\
             include_bytes!(\"codebooks/fin_voice_codebooks.fvcb\");\n"
    )
}

fn render_manifest(set: &CodebookSet, payload_crc32: u32) -> String {
    let mut out = String::new();
    let _ = writeln!(out, "{{");
    let _ = writeln!(out, "  \"artifact\": \"FinTally Voice Codebook\",");
    let _ = writeln!(out, "  \"format\": \"FVCB\",");
    let _ = writeln!(out, "  \"format_version\": {CODEBOOK_FORMAT_VERSION},");
    let _ = writeln!(
        out,
        "  \"codebook_schema_version\": {},",
        set.schema_version
    );
    let _ = writeln!(out, "  \"feature_contract\": {{");
    let _ = writeln!(
        out,
        "    \"feature_schema_version\": {FEATURE_SCHEMA_VERSION},"
    );
    let _ = writeln!(out, "    \"sample_rate_hz\": {CANONICAL_SAMPLE_RATE_HZ},");
    let _ = writeln!(
        out,
        "    \"channels\": {},",
        crate::dataset::CANONICAL_CHANNELS
    );
    let _ = writeln!(out, "    \"frame_samples\": {},", features::FRAME_SAMPLES);
    let _ = writeln!(out, "    \"hop_samples\": {},", features::HOP_SAMPLES);
    let _ = writeln!(out, "    \"feature_dimension\": {}", set.feature_dim);
    let _ = writeln!(out, "  }},");
    let _ = writeln!(
        out,
        "  \"normalization_schema_version\": {},",
        set.normalization.schema_version
    );
    let _ = writeln!(
        out,
        "  \"normalization_training_frame_count\": {},",
        set.normalization.training_frame_count
    );
    let _ = writeln!(out, "  \"kmeans_seed\": {},", set.seed);
    let _ = writeln!(out, "  \"payload_crc32\": \"{payload_crc32:08x}\",");
    let _ = writeln!(out, "  \"languages\": [");

    for (index, language) in LANGUAGES.iter().copied().enumerate() {
        if let Some(codebook) = set.get(language) {
            let (_, language_text) = language_code(language);
            let comma = if index + 1 == LANGUAGES.len() {
                ""
            } else {
                ","
            };
            let _ = writeln!(out, "    {{");
            let _ = writeln!(out, "      \"language\": \"{language_text}\",");
            let _ = writeln!(out, "      \"dimensions\": {},", codebook.dimensions);
            let _ = writeln!(
                out,
                "      \"centroid_count\": {},",
                codebook.centroids.len()
            );
            let _ = writeln!(
                out,
                "      \"training_samples\": {},",
                codebook.training_samples
            );
            let _ = writeln!(out, "      \"iterations\": {},", codebook.iterations);
            let _ = writeln!(out, "      \"converged\": {},", codebook.converged);
            let _ = writeln!(
                out,
                "      \"inertia\": {},",
                format_json_f64(codebook.inertia)
            );
            let _ = writeln!(out, "      \"backend\": \"{}\"", codebook.backend.as_str());
            let _ = writeln!(out, "    }}{comma}");
        }
    }
    let _ = writeln!(out, "  ]");
    let _ = writeln!(out, "}}");
    out
}

fn centroid_count(set: &CodebookSet, language: LanguageId) -> Result<usize> {
    Ok(set
        .get(language)
        .with_context(|| format!("missing required {language} codebook"))?
        .centroids
        .len())
}

fn language_code(language: LanguageId) -> (u8, &'static str) {
    match language {
        LanguageId::En => (1, "en"),
        LanguageId::Fr => (2, "fr"),
        LanguageId::Hi => (3, "hi"),
    }
}

fn centroid_symbol(language: LanguageId) -> &'static str {
    match language {
        LanguageId::En => "fin_voice_en_centroids",
        LanguageId::Fr => "fin_voice_fr_centroids",
        LanguageId::Hi => "fin_voice_hi_centroids",
    }
}

fn centroid_count_macro(language: LanguageId) -> &'static str {
    match language {
        LanguageId::En => "FIN_VOICE_CODEBOOK_EN_CENTROID_COUNT",
        LanguageId::Fr => "FIN_VOICE_CODEBOOK_FR_CENTROID_COUNT",
        LanguageId::Hi => "FIN_VOICE_CODEBOOK_HI_CENTROID_COUNT",
    }
}

fn backend_code(backend: ComputeBackend) -> u8 {
    match backend {
        ComputeBackend::RayonCpu => 1,
        ComputeBackend::Cuda => 2,
    }
}

fn to_u32(value: usize, description: &str) -> Result<u32> {
    u32::try_from(value).with_context(|| format!("{description} exceeds the FVCB format limit"))
}

fn push_u32(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn push_f32(out: &mut Vec<u8>, value: f32) {
    out.extend_from_slice(&value.to_le_bytes());
}

fn format_c_f32(value: f32) -> String {
    // Nine digits after the decimal in scientific notation are enough to
    // round-trip every finite FP32 value through a C decimal literal.
    format!("{value:.9e}f")
}

fn format_c_f64(value: f64) -> String {
    format!("{value:.17e}")
}

fn format_json_f64(value: f64) -> String {
    format!("{value:.17e}")
}

/// IEEE CRC-32 (polynomial 0xEDB88320), calculated only over the payload.
fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = !0_u32;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            let mask = 0_u32.wrapping_sub(crc & 1);
            crc = (crc >> 1) ^ (0xEDB8_8320 & mask);
        }
    }
    !crc
}

fn write_atomic(path: &Path, bytes: &[u8]) -> Result<()> {
    let parent = path
        .parent()
        .context("generated file path does not have a parent directory")?;
    fs::create_dir_all(parent)
        .with_context(|| format!("failed to create directory {}", parent.display()))?;

    let file_name = path
        .file_name()
        .and_then(|name| name.to_str())
        .context("generated file name is not valid UTF-8")?;
    let temporary_path = path.with_file_name(format!(".{file_name}.tmp-{}", std::process::id()));

    let result = (|| -> Result<()> {
        let mut file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temporary_path)
            .with_context(|| {
                format!(
                    "failed to create temporary file {}",
                    temporary_path.display()
                )
            })?;
        file.write_all(bytes).with_context(|| {
            format!(
                "failed to write temporary file {}",
                temporary_path.display()
            )
        })?;
        file.sync_all().with_context(|| {
            format!("failed to sync temporary file {}", temporary_path.display())
        })?;
        drop(file);
        fs::rename(&temporary_path, path).with_context(|| {
            format!(
                "failed to atomically replace {} with {}",
                path.display(),
                temporary_path.display()
            )
        })?;
        Ok(())
    })();

    if result.is_err() {
        let _ = fs::remove_file(&temporary_path);
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32_matches_standard_check_value() {
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
    }

    #[test]
    fn format_constants_match_documented_header_size() {
        assert_eq!(ARTIFACT_MAGIC, b"FVCB\r\n\x1a\n");
        assert_eq!(ARTIFACT_HEADER_SIZE, 84);
    }

    #[test]
    fn fp32_c_literal_has_suffix_and_exponent() {
        let literal = format_c_f32(1.25);
        assert!(literal.ends_with('f'));
        assert!(literal.contains('e'));
    }
}
