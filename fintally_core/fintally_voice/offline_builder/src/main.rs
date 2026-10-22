//! Fintally Voice — Offline Codebook Builder
//!
//! Offline-only training and dataset preparation executable.
//
//! Responsibilities of this file:
//!
//! - CLI argument parsing
//! - global builder configuration
//! - pipeline orchestration
//! - deterministic execution
//! - high-level logging / progress
//!
//! Intentionally NOT implemented here:
//!
//! - TSV parsing
//! - audio decoding
//! - resampling
//! - DSP feature extraction
//! - K-means implementation
//! - validation
//! - codebook serialization
//!
//! Those belong to dedicated modules so the offline builder remains modular
//! and the runtime voice engine stays completely independent from the training
//! environment.

use std::path::{Path, PathBuf};
use std::time::Instant;

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use tracing::{info, warn};
use tracing_subscriber::EnvFilter;

// =============================================================================
// MODULES
// =============================================================================
//
// These modules will be added as the builder is expanded.
//
// Keep this list explicit. The offline builder is intentionally not a giant
// single source file.
//

mod audio;
mod dataset;
mod export;
mod features;
#[cfg(feature = "cuda")]
mod gpu;
mod kmeans;
mod validation;

// =============================================================================
// APPLICATION CONSTANTS
// =============================================================================

const DEFAULT_HOURS_PER_LANGUAGE: f64 = 25.0;

const DEFAULT_SAMPLE_RATE: u32 = 16_000;

const DEFAULT_CHANNELS: u16 = 1;

const DEFAULT_FEATURE_DIM: usize = 32;

const DEFAULT_CODEBOOK_SIZE: usize = 128;

/// Tested maximum that allows the current English codebook to reach convergence.
/// Individual languages stop early when the convergence criteria are satisfied.
const DEFAULT_KMEANS_ITERATIONS: usize = 500;

const DEFAULT_KMEANS_SEED: u64 = 0xF1_0C_A11;

const DEFAULT_WORKERS: usize = 0;

// =============================================================================
// CLI
// =============================================================================

#[derive(Debug, Parser)]
#[command(
    name = "fintally-offline-builder",
    version,
    about = "Fintally Voice offline dataset and codebook builder",
    long_about = None,
)]
struct Cli {
    /// Offline builder command.
    #[command(subcommand)]
    command: Command,

    /// Root directory containing raw datasets.
    #[arg(long, default_value = "data_raw")]
    raw_dir: PathBuf,

    /// Directory containing prepared float32 WAV training data and manifest.
    #[arg(long, default_value = "data")]
    data_dir: PathBuf,

    /// Directory for generated codebooks, manifests and reports.
    #[arg(long, default_value = "output")]
    output_dir: PathBuf,

    /// Maximum amount of source audio selected per language.
    #[arg(long, default_value_t = DEFAULT_HOURS_PER_LANGUAGE)]
    hours_per_language: f64,

    /// Final training sample rate.
    #[arg(long, default_value_t = DEFAULT_SAMPLE_RATE)]
    sample_rate: u32,

    /// Final number of audio channels.
    #[arg(long, default_value_t = DEFAULT_CHANNELS)]
    channels: u16,

    /// Feature-vector dimensionality expected by the VQ engine.
    #[arg(long, default_value_t = DEFAULT_FEATURE_DIM)]
    feature_dim: usize,

    /// Number of centroids in the generated codebook.
    #[arg(long, default_value_t = DEFAULT_CODEBOOK_SIZE)]
    codebook_size: usize,

    /// Maximum K-means iterations.
    #[arg(long, default_value_t = DEFAULT_KMEANS_ITERATIONS)]
    kmeans_iterations: usize,

    /// Deterministic K-means seed.
    #[arg(long, default_value_t = DEFAULT_KMEANS_SEED)]
    kmeans_seed: u64,

    /// Number of CPU workers.
    ///
    /// 0 means automatic detection.
    ///
    /// This is intentionally configurable because the training machine and
    /// eventual embedded target have radically different constraints.
    #[arg(long, default_value_t = DEFAULT_WORKERS)]
    workers: usize,

    /// Train and validate without exporting the .fvcb artifact, manifest, or generated runtime C/Rust tables.
    /// The validation report is still written; use this for comparison runs that must preserve the deployed model.
    #[arg(long)]
    no_export: bool,

    /// Enable verbose logging.
    #[arg(short, long)]
    verbose: bool,
}

// =============================================================================
// COMMANDS
// =============================================================================

#[derive(Debug, Subcommand)]
enum Command {
    /// Validate the offline builder environment and dataset structure.
    Check,

    /// Prepare raw audio into deterministic 16 kHz mono float32 WAV files.
    Prepare,

    /// Extract features, train codebooks and validate them.
    Train,

    /// Run preparation followed by training and export.
    All,

    /// Remove generated prepared data and/or builder output.
    Clean {
        /// Remove prepared WAV data.
        #[arg(long)]
        data: bool,

        /// Remove generated codebooks/reports.
        #[arg(long)]
        output: bool,
    },
}

// =============================================================================
// BUILDER CONFIGURATION
// =============================================================================

#[derive(Debug, Clone)]
pub struct BuilderConfig {
    pub raw_dir: PathBuf,
    pub data_dir: PathBuf,
    pub output_dir: PathBuf,

    pub hours_per_language: f64,

    pub sample_rate: u32,
    pub channels: u16,

    pub feature_dim: usize,

    pub codebook_size: usize,
    pub kmeans_iterations: usize,
    pub kmeans_seed: u64,

    pub workers: usize,

    /// Whether to publish the generated codebooks to runtime C/Rust paths.
    /// Disable for controlled validation-only comparison runs.
    pub export_codebooks: bool,
}

// =============================================================================
// MAIN
// =============================================================================

fn main() -> Result<()> {
    let cli = Cli::parse();

    init_logging(cli.verbose)?;

    let config = BuilderConfig {
        raw_dir: cli.raw_dir,
        data_dir: cli.data_dir,
        output_dir: cli.output_dir,

        hours_per_language: cli.hours_per_language,

        sample_rate: cli.sample_rate,
        channels: cli.channels,

        feature_dim: cli.feature_dim,

        codebook_size: cli.codebook_size,
        kmeans_iterations: cli.kmeans_iterations,
        kmeans_seed: cli.kmeans_seed,

        workers: cli.workers,
        export_codebooks: !cli.no_export,
    };

    validate_config(&config)?;

    match cli.command {
        Command::Check => run_check(&config),

        Command::Prepare => run_prepare(&config),

        Command::Train => run_train(&config),

        Command::All => run_all(&config),

        Command::Clean { data, output } => run_clean(&config, data, output),
    }
}

// =============================================================================
// LOGGING
// =============================================================================

fn init_logging(verbose: bool) -> Result<()> {
    let default_filter = if verbose { "info" } else { "warn" };

    let filter =
        EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new(default_filter));

    tracing_subscriber::fmt()
        .with_env_filter(filter)
        .with_target(false)
        .compact()
        .try_init()
        .map_err(|err| anyhow::anyhow!("failed to initialize logging: {}", err))?;

    Ok(())
}

// =============================================================================
// CONFIGURATION VALIDATION
// =============================================================================

fn validate_config(config: &BuilderConfig) -> Result<()> {
    anyhow::ensure!(
        config.hours_per_language.is_finite() && config.hours_per_language > 0.0,
        "hours_per_language must be finite and greater than zero"
    );

    anyhow::ensure!(
        config.sample_rate == dataset::CANONICAL_SAMPLE_RATE_HZ,
        "offline builder currently requires --sample-rate {}",
        dataset::CANONICAL_SAMPLE_RATE_HZ
    );

    anyhow::ensure!(
        config.channels == dataset::CANONICAL_CHANNELS,
        "offline builder currently requires --channels {} (mono)",
        dataset::CANONICAL_CHANNELS
    );

    anyhow::ensure!(
        config.feature_dim == dataset::CANONICAL_FEATURE_DIMENSION,
        "native DSP ABI produces {} features; --feature-dim must match",
        dataset::CANONICAL_FEATURE_DIMENSION
    );

    anyhow::ensure!(
        config.codebook_size > 1,
        "codebook_size must be greater than one"
    );

    anyhow::ensure!(
        config.kmeans_iterations > 0,
        "kmeans_iterations must be greater than zero"
    );

    Ok(())
}

// =============================================================================
// CHECK
// =============================================================================

fn run_check(config: &BuilderConfig) -> Result<()> {
    let started = Instant::now();

    info!("Fintally Voice offline builder environment check");

    ensure_directory_exists(&config.raw_dir, "raw dataset directory")?;

    ensure_directory_exists(&config.data_dir, "prepared data directory")?;

    ensure_directory_exists(&config.output_dir, "output directory")?;

    info!(
        raw_dir = %config.raw_dir.display(),
        data_dir = %config.data_dir.display(),
        output_dir = %config.output_dir.display(),
        "builder directories are available"
    );

    info!(
        hours_per_language = config.hours_per_language,
        sample_rate = config.sample_rate,
        channels = config.channels,
        feature_dim = config.feature_dim,
        codebook_size = config.codebook_size,
        kmeans_iterations = config.kmeans_iterations,
        workers = config.workers,
        "builder configuration"
    );

    // Verify that the enabled English/French SPS and Hindi Common Voice inputs
    // are present before any audio preparation begins.
    dataset::check_layout(&config.raw_dir).context("dataset layout check failed")?;

    // Audio backend validation verifies the in-process Symphonia/Rubato stack.
    audio::check_backend().context("audio backend check failed")?;

    info!(
        elapsed_ms = started.elapsed().as_millis(),
        "environment check complete"
    );

    Ok(())
}

// =============================================================================
// PREPARE
// =============================================================================

fn run_prepare(config: &BuilderConfig) -> Result<()> {
    let started = Instant::now();

    print_header("PREPARE DATASET", config);

    ensure_directory_exists(&config.raw_dir, "raw dataset directory")?;

    std::fs::create_dir_all(&config.data_dir).with_context(|| {
        format!(
            "failed to create prepared-data directory: {}",
            config.data_dir.display()
        )
    })?;

    dataset::check_layout(&config.raw_dir).context("dataset layout is invalid")?;

    // Dataset selection is responsible for English/French SPS and Hindi
    // Common Voice. Duration limits use metadata durations, and selection
    // reserves independent budgets for train/validation/test splits.
    let selection = dataset::select_training_clips(&config.raw_dir, config.hours_per_language)
        .context("failed to select training clips")?;

    info!(
        languages = selection.languages(),
        "dataset selection complete"
    );

    let preparation_config = audio::PrepareConfig {
        output_sample_rate: config.sample_rate,
        output_channels: config.channels,
    };

    let summary = audio::prepare_dataset(&selection, &config.data_dir, &preparation_config)
        .context("audio dataset preparation failed")?;

    info!(
        files = summary.files_written,
        skipped = summary.files_skipped,
        failed = summary.files_failed,
        "audio preparation complete"
    );

    info!(
        elapsed_ms = started.elapsed().as_millis(),
        "dataset preparation finished"
    );

    Ok(())
}

// =============================================================================
// TRAIN
// =============================================================================

fn run_train(config: &BuilderConfig) -> Result<()> {
    let started = Instant::now();

    print_header("TRAIN CODEBOOKS", config);

    ensure_directory_exists(&config.data_dir, "prepared data directory")?;

    std::fs::create_dir_all(&config.output_dir).with_context(|| {
        format!(
            "failed to create output directory: {}",
            config.output_dir.display()
        )
    })?;

    // -------------------------------------------------------------------------
    // 1. Discover prepared training data
    // -------------------------------------------------------------------------

    let dataset = dataset::load_prepared_dataset(&config.data_dir)
        .context("failed to load prepared dataset")?;

    info!(languages = dataset.languages(), "prepared dataset loaded");

    // -------------------------------------------------------------------------
    // 2. Feature extraction
    // -------------------------------------------------------------------------

    let feature_config = features::FeatureConfig {
        sample_rate: config.sample_rate,
        feature_dim: config.feature_dim,
    };

    let feature_sets =
        features::extract_dataset_features(&dataset, &feature_config, config.workers)
            .context("feature extraction failed")?;

    info!("feature extraction complete");

    // -------------------------------------------------------------------------
    // 3. Feature normalization
    // -------------------------------------------------------------------------

    let normalized =
        features::normalize_feature_sets(feature_sets).context("feature normalization failed")?;

    info!("feature normalization complete");

    // -------------------------------------------------------------------------
    // 4. K-means
    // -------------------------------------------------------------------------

    let kmeans_config = kmeans::KMeansConfig {
        dimensions: config.feature_dim,
        clusters: config.codebook_size,
        max_iterations: config.kmeans_iterations,
        seed: config.kmeans_seed,
        workers: config.workers,
    };

    let codebooks = kmeans::train_codebooks(&normalized, &kmeans_config)
        .context("K-means codebook training failed")?;

    info!(codebooks = codebooks.len(), "K-means training complete");

    // -------------------------------------------------------------------------
    // 5. Validation
    // -------------------------------------------------------------------------

    let validation_report = validation::validate_codebooks(&normalized, &codebooks)
        .context("codebook validation failed")?;

    validation::write_report(&validation_report, &config.output_dir)
        .context("failed to write validation report")?;

    info!("codebook validation complete");

    // -------------------------------------------------------------------------
    // 6. Export
    // -------------------------------------------------------------------------

    if config.export_codebooks {
        export::export_codebooks(&codebooks, &config.output_dir)
            .context("failed to export codebooks")?;
    } else {
        info!(
            output_dir = %config.output_dir.display(),
            "codebook artifact export skipped; existing runtime C/Rust codebooks were left unchanged"
        );
    }

    // -------------------------------------------------------------------------
    // Summary
    // -------------------------------------------------------------------------

    info!(
        output_dir = %config.output_dir.display(),
        elapsed_ms = started.elapsed().as_millis(),
        "codebook training finished"
    );

    Ok(())
}

// =============================================================================
// ALL
// =============================================================================

fn run_all(config: &BuilderConfig) -> Result<()> {
    let started = Instant::now();

    print_header("FULL OFFLINE PIPELINE", config);

    run_prepare(config)?;
    run_train(config)?;

    info!(
        elapsed_ms = started.elapsed().as_millis(),
        "full offline pipeline complete"
    );

    Ok(())
}

// =============================================================================
// CLEAN
// =============================================================================

fn run_clean(config: &BuilderConfig, clean_data: bool, clean_output: bool) -> Result<()> {
    anyhow::ensure!(
        clean_data || clean_output,
        "nothing selected for cleaning; use --data and/or --output"
    );

    if clean_data {
        remove_directory_contents(&config.data_dir, "prepared data")?;
    }

    if clean_output {
        remove_directory_contents(&config.output_dir, "builder output")?;
    }

    Ok(())
}

// =============================================================================
// HELPERS
// =============================================================================

fn ensure_directory_exists(path: &Path, description: &str) -> Result<()> {
    anyhow::ensure!(
        path.is_dir(),
        "{} does not exist or is not a directory: {}",
        description,
        path.display()
    );

    Ok(())
}

fn remove_directory_contents(path: &Path, description: &str) -> Result<()> {
    if !path.exists() {
        warn!(
            directory = %path.display(),
            "{} directory does not exist; nothing to remove",
            description
        );

        return Ok(());
    }

    for entry in std::fs::read_dir(path).with_context(|| {
        format!(
            "failed to read {} directory: {}",
            description,
            path.display()
        )
    })? {
        let entry = entry.context("failed to read directory entry")?;

        let entry_path = entry.path();

        if entry_path.is_dir() {
            std::fs::remove_dir_all(&entry_path)
                .with_context(|| format!("failed to remove directory: {}", entry_path.display()))?;
        } else {
            std::fs::remove_file(&entry_path)
                .with_context(|| format!("failed to remove file: {}", entry_path.display()))?;
        }
    }

    info!(
        directory = %path.display(),
        "cleared {}",
        description
    );

    Ok(())
}

fn print_header(title: &str, config: &BuilderConfig) {
    println!();
    println!("============================================================");
    println!(" FINTALLY VOICE — OFFLINE BUILDER");
    println!("============================================================");
    println!(" Stage           : {}", title);
    println!(" Raw data        : {}", config.raw_dir.display());
    println!(" Prepared data   : {}", config.data_dir.display());
    println!(" Output          : {}", config.output_dir.display());
    println!(" Hours/language  : {:.2}", config.hours_per_language);
    println!(" Sample rate     : {} Hz", config.sample_rate);
    println!(" Channels        : {}", config.channels);
    println!(" Feature dim     : {}", config.feature_dim);
    println!(" Codebook size   : {}", config.codebook_size);
    println!(" K-means iters   : {}", config.kmeans_iterations);
    println!(" K-means seed    : {}", config.kmeans_seed);
    println!(
        " Runtime export  : {}",
        if config.export_codebooks {
            "enabled"
        } else {
            "disabled (validation-only)"
        }
    );

    if config.workers == 0 {
        println!(" CPU workers     : automatic");
    } else {
        println!(" CPU workers     : {}", config.workers);
    }

    println!("============================================================");
    println!();
}
