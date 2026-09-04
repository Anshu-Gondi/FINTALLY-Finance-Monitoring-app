use anyhow::{bail, Context, Result};
use hf_hub::api::sync::{Api, ApiBuilder};

use std::env;
use std::fs;
use std::path::{Component, Path};
use std::process::Command;
use std::thread;
use std::time::Duration;

// =============================================================================
// Configuration
// =============================================================================

const DEFAULT_MAX_RETRIES: u32 = 5;
const DEFAULT_BACKOFF_MS: u64 = 1500;

const QWEN_OUTPUT_DIR: &str = "llm_models/chat/qwen_safetensors_output";
const BGE_OUTPUT_DIR: &str = "llm_models/embedding/bge_safetensors_output";

const GIB: u64 = 1024 * 1024 * 1024;

// =============================================================================
// Hardware profile
// =============================================================================

#[derive(Debug, Clone, Copy)]
struct GpuProfile {
    vram_bytes: u64,
    power_limit_watts: Option<f64>,
}

impl GpuProfile {
    fn vram_gb(self) -> f64 {
        self.vram_bytes as f64 / GIB as f64
    }
}

#[derive(Debug, Clone, Copy)]
struct HardwareProfile {
    ram_bytes: Option<u64>,
    gpu: Option<GpuProfile>,
}

impl HardwareProfile {
    fn ram_gb(self) -> Option<f64> {
        self.ram_bytes.map(|bytes| bytes as f64 / GIB as f64)
    }

    fn vram_gb(self) -> f64 {
        self.gpu.map(GpuProfile::vram_gb).unwrap_or(0.0)
    }

    fn power_watts(self) -> Option<f64> {
        self.gpu.and_then(|gpu| gpu.power_limit_watts)
    }
}

// =============================================================================
// Model configuration
// =============================================================================

#[derive(Debug, Clone, Copy)]
struct ModelConfig {
    repo_id: &'static str,
    param_size: &'static str,
    quantization: &'static str,

    /// Exact repository files required by the runtime.
    ///
    /// This is intentionally explicit. We do not scan a model repository and
    /// download "anything ending in .json".
    files: &'static [&'static str],
}

// Qwen 2.5 7B.
//
// IMPORTANT:
// The current Q4_K_M and Q8_0 artifacts are sharded.
const QWEN_7B_Q4_K_M_FILES: &[&str] = &[
    "qwen2.5-7b-instruct-q4_k_m-00001-of-00002.gguf",
    "qwen2.5-7b-instruct-q4_k_m-00002-of-00002.gguf",
];

const QWEN_7B_Q8_0_FILES: &[&str] = &[
    "qwen2.5-7b-instruct-q8_0-00001-of-00003.gguf",
    "qwen2.5-7b-instruct-q8_0-00002-of-00003.gguf",
    "qwen2.5-7b-instruct-q8_0-00003-of-00003.gguf",
];

// Qwen 2.5 3B
const QWEN_3B_Q4_K_M_FILES: &[&str] =
    &["qwen2.5-3b-instruct-q4_k_m.gguf"];

const QWEN_3B_Q8_0_FILES: &[&str] =
    &["qwen2.5-3b-instruct-q8_0.gguf"];

// Qwen 2.5 1.5B
const QWEN_1_5B_Q4_K_M_FILES: &[&str] =
    &["qwen2.5-1.5b-instruct-q4_k_m.gguf"];

const QWEN_1_5B_Q8_0_FILES: &[&str] =
    &["qwen2.5-1.5b-instruct-q8_0.gguf"];

// Qwen 2.5 0.5B
const QWEN_0_5B_Q4_K_M_FILES: &[&str] =
    &["qwen2.5-0.5b-instruct-q4_k_m.gguf"];

const QWEN_0_5B_Q8_0_FILES: &[&str] =
    &["qwen2.5-0.5b-instruct-q8_0.gguf"];

// =============================================================================
// BGE manifest
// =============================================================================

const BGE_FILES: &[&str] = &[
    "1_Pooling/config.json",
    "config.json",
    "config_sentence_transformers.json",
    "model.safetensors",
    "modules.json",
    "sentence_bert_config.json",
    "special_tokens_map.json",
    "tokenizer.json",
    "tokenizer_config.json",
    "vocab.txt",
];

// =============================================================================
// Hardware detection
// =============================================================================

fn detect_linux_ram_bytes() -> Option<u64> {
    let contents = fs::read_to_string("/proc/meminfo").ok()?;

    for line in contents.lines() {
        let mut parts = line.split_whitespace();

        let key = parts.next()?;

        if key != "MemTotal:" {
            continue;
        }

        let kb = parts.next()?.parse::<u64>().ok()?;

        return kb.checked_mul(1024);
    }

    None
}

fn detect_macos_ram_bytes() -> Option<u64> {
    let output = Command::new("sysctl")
        .args(["-n", "hw.memsize"])
        .output()
        .ok()?;

    if !output.status.success() {
        return None;
    }

    String::from_utf8(output.stdout)
        .ok()?
        .trim()
        .parse::<u64>()
        .ok()
}

fn detect_windows_ram_bytes() -> Option<u64> {
    // PowerShell is available on normal Windows installations.
    //
    // -NoProfile + -NonInteractive avoids user profile startup overhead and
    // prevents interactive prompts from blocking the installer.
    let output = Command::new("powershell.exe")
        .args([
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            "(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory",
        ])
        .output()
        .ok()?;

    if !output.status.success() {
        return None;
    }

    String::from_utf8(output.stdout)
        .ok()?
        .trim()
        .parse::<u64>()
        .ok()
}

fn detect_system_ram_bytes() -> Option<u64> {
    // Explicit override is useful in containers/CI.
    if let Ok(value) = env::var("FINTALLY_RAM_BYTES") {
        if let Ok(bytes) = value.trim().parse::<u64>() {
            if bytes > 0 {
                return Some(bytes);
            }
        }
    }

    if cfg!(target_os = "linux") {
        return detect_linux_ram_bytes();
    }

    if cfg!(target_os = "macos") {
        return detect_macos_ram_bytes();
    }

    if cfg!(target_os = "windows") {
        return detect_windows_ram_bytes();
    }

    None
}

fn parse_positive_watts(value: &str) -> Option<f64> {
    let cleaned = value
        .trim()
        .trim_matches(|c: char| c == '"' || c == '\'');

    let first_numeric = cleaned
        .split_whitespace()
        .next()
        .unwrap_or(cleaned);

    first_numeric
        .parse::<f64>()
        .ok()
        .filter(|watts| watts.is_finite() && *watts > 0.0)
}

fn query_nvidia_power_limit(gpu_index: usize) -> Option<f64> {
    // Primary path: machine-readable query.
    //
    // Some laptop / virtualized / driver combinations report power.limit as
    // N/A even though other power-limit fields are available. Try the current
    // limit first, then default and maximum limits.
    let output = Command::new("nvidia-smi")
        .args([
            "-i",
            &gpu_index.to_string(),
            "--query-gpu=power.limit,power.default_limit,power.max_limit",
            "--format=csv,noheader,nounits",
        ])
        .output()
        .ok()?;

    if output.status.success() {
        if let Ok(stdout) = String::from_utf8(output.stdout) {
            if let Some(line) = stdout.lines().next() {
                let fields: Vec<&str> = line.split(',').collect();

                // Preference:
                //   0 = current configured limit
                //   1 = default board/vendor limit
                //   2 = maximum permitted limit
                for field in fields {
                    if let Some(watts) = parse_positive_watts(field) {
                        return Some(watts);
                    }
                }
            }
        }
    }

    // Fallback path: human-readable power section.
    //
    // This is useful on drivers where one or more CSV query fields are N/A.
    let output = Command::new("nvidia-smi")
        .args(["-i", &gpu_index.to_string(), "-q", "-d", "POWER"])
        .output()
        .ok()?;

    if !output.status.success() {
        return None;
    }

    let stdout = String::from_utf8(output.stdout).ok()?;

    let preferred_keys = [
        "Current Power Limit",
        "Power Limit",
        "Default Power Limit",
        "Max Power Limit",
    ];

    for key in preferred_keys {
        for line in stdout.lines() {
            let trimmed = line.trim();

            if !trimmed.starts_with(key) {
                continue;
            }

            if let Some((_, value)) = trimmed.split_once(':') {
                if let Some(watts) = parse_positive_watts(value) {
                    return Some(watts);
                }
            }
        }
    }

    None
}

fn detect_nvidia_gpu() -> Option<GpuProfile> {
    let output = Command::new("nvidia-smi")
        .args([
            "--query-gpu=index,memory.total",
            "--format=csv,noheader,nounits",
        ])
        .output()
        .ok()?;

    if !output.status.success() {
        return None;
    }

    let stdout = String::from_utf8(output.stdout).ok()?;

    let mut best_gpu: Option<GpuProfile> = None;

    for line in stdout.lines() {
        let mut fields = line.split(',');

        let gpu_index = match fields.next().and_then(|value| value.trim().parse::<usize>().ok()) {
            Some(index) => index,
            None => continue,
        };

        let memory_mb = match fields.next().and_then(|value| value.trim().parse::<u64>().ok()) {
            Some(value) if value > 0 => value,
            _ => continue,
        };

        let power_limit_watts = query_nvidia_power_limit(gpu_index);

        let candidate = GpuProfile {
            vram_bytes: memory_mb.saturating_mul(1024 * 1024),
            power_limit_watts,
        };

        match best_gpu {
            None => best_gpu = Some(candidate),

            Some(current) if candidate.vram_bytes > current.vram_bytes => {
                best_gpu = Some(candidate);
            }

            _ => {}
        }
    }

    best_gpu
}

fn detect_hardware() -> HardwareProfile {
    HardwareProfile {
        ram_bytes: detect_system_ram_bytes(),
        gpu: detect_nvidia_gpu(),
    }
}

// =============================================================================
// Model selection
// =============================================================================

fn select_optimal_qwen_model(hw: HardwareProfile) -> ModelConfig {
    let ram_gb = hw.ram_gb().unwrap_or(0.0);
    let vram_gb = hw.vram_gb();
    let power_watts = hw.power_watts();

    println!(
        "📊 Hardware -> RAM: {} | VRAM: {:.1} GB | GPU Power Limit: {}",
        match hw.ram_gb() {
            Some(value) => format!("{value:.1} GB"),
            None => "unknown".to_owned(),
        },
        vram_gb,
        match power_watts {
            Some(value) => format!("{value:.0} W"),
            None => "unknown".to_owned(),
        }
    );

    // -------------------------------------------------------------------------
    // Production policy:
    //
    // 1. RAM decides the maximum model family.
    // 2. Q4_K_M is the default safe quantization.
    // 3. Q8_0 is selected only when there is both enough RAM and a sufficiently
    //    large, reasonably powered NVIDIA GPU.
    //
    // This avoids the old "75 W if unknown" behavior.
    // -------------------------------------------------------------------------

    if ram_gb >= 16.0 {
        let can_run_7b_q8 = vram_gb >= 12.0
            && power_watts
                .map(|watts| watts >= 90.0)
                .unwrap_or(false);

        if can_run_7b_q8 {
            return ModelConfig {
                repo_id: "Qwen/Qwen2.5-7B-Instruct-GGUF",
                param_size: "7B",
                quantization: "Q8_0",
                files: QWEN_7B_Q8_0_FILES,
            };
        }

        return ModelConfig {
            repo_id: "Qwen/Qwen2.5-7B-Instruct-GGUF",
            param_size: "7B",
            quantization: "Q4_K_M",
            files: QWEN_7B_Q4_K_M_FILES,
        };
    }

    if ram_gb >= 8.0 {
        let can_run_3b_q8 = vram_gb >= 6.0
            && power_watts
                .map(|watts| watts >= 80.0)
                .unwrap_or(false);

        if can_run_3b_q8 {
            return ModelConfig {
                repo_id: "Qwen/Qwen2.5-3B-Instruct-GGUF",
                param_size: "3B",
                quantization: "Q8_0",
                files: QWEN_3B_Q8_0_FILES,
            };
        }

        return ModelConfig {
            repo_id: "Qwen/Qwen2.5-3B-Instruct-GGUF",
            param_size: "3B",
            quantization: "Q4_K_M",
            files: QWEN_3B_Q4_K_M_FILES,
        };
    }

    if ram_gb >= 4.0 {
        let can_run_1_5b_q8 = vram_gb >= 4.0
            && power_watts
                .map(|watts| watts >= 65.0)
                .unwrap_or(false);

        if can_run_1_5b_q8 {
            return ModelConfig {
                repo_id: "Qwen/Qwen2.5-1.5B-Instruct-GGUF",
                param_size: "1.5B",
                quantization: "Q8_0",
                files: QWEN_1_5B_Q8_0_FILES,
            };
        }

        return ModelConfig {
            repo_id: "Qwen/Qwen2.5-1.5B-Instruct-GGUF",
            param_size: "1.5B",
            quantization: "Q4_K_M",
            files: QWEN_1_5B_Q4_K_M_FILES,
        };
    }

    ModelConfig {
        repo_id: "Qwen/Qwen2.5-0.5B-Instruct-GGUF",
        param_size: "0.5B",
        quantization: "Q4_K_M",
        files: QWEN_0_5B_Q4_K_M_FILES,
    }
}

// =============================================================================
// Hugging Face downloader
// =============================================================================

struct HuggingFaceDownloader {
    api: Api,
    max_retries: u32,
    backoff_ms: u64,
}

impl HuggingFaceDownloader {
    fn new() -> Result<Self> {
        let token = env::var("hugging_face_read_only_key")
            .or_else(|_| env::var("HF_TOKEN"))
            .ok()
            .map(|value| value.trim().to_owned())
            .filter(|value| !value.is_empty());

        let mut builder = ApiBuilder::new().with_progress(true);

        if let Some(token) = token {
            builder = builder.with_token(Some(token));
        }

        let api = builder
            .build()
            .context("failed to initialize Hugging Face client")?;

        Ok(Self {
            api,
            max_retries: env::var("FINTALLY_HF_MAX_RETRIES")
                .ok()
                .and_then(|value| value.parse::<u32>().ok())
                .filter(|value| *value > 0)
                .unwrap_or(DEFAULT_MAX_RETRIES),

            backoff_ms: env::var("FINTALLY_HF_BACKOFF_MS")
                .ok()
                .and_then(|value| value.parse::<u64>().ok())
                .filter(|value| *value > 0)
                .unwrap_or(DEFAULT_BACKOFF_MS),
        })
    }

    fn retry<T, F>(&self, operation: &str, mut action: F) -> Result<T>
    where
        F: FnMut() -> Result<T>,
    {
        let mut last_error: Option<anyhow::Error> = None;

        for attempt in 1..=self.max_retries {
            match action() {
                Ok(value) => return Ok(value),

                Err(error) => {
                    last_error = Some(error);

                    if attempt == self.max_retries {
                        break;
                    }

                    let shift = (attempt - 1).min(5);
                    let multiplier = 1_u64 << shift;

                    let delay_ms = self
                        .backoff_ms
                        .saturating_mul(multiplier);

                    eprintln!(
                        "⚠️ {} failed (attempt {}/{}). Retrying in {:.1}s...",
                        operation,
                        attempt,
                        self.max_retries,
                        delay_ms as f64 / 1000.0
                    );

                    thread::sleep(Duration::from_millis(delay_ms));
                }
            }
        }

        Err(last_error.unwrap_or_else(|| {
            anyhow::anyhow!("{} failed without an error", operation)
        }))
        .with_context(|| format!("{} failed after {} attempts", operation, self.max_retries))
    }

    fn validate_relative_repo_path(path: &str) -> Result<()> {
        let candidate = Path::new(path);

        if candidate.is_absolute() {
            bail!("refusing absolute repository path: {}", path);
        }

        for component in candidate.components() {
            match component {
                Component::Normal(_) | Component::CurDir => {}

                Component::ParentDir => {
                    bail!("refusing path traversal in repository filename: {}", path);
                }

                Component::RootDir | Component::Prefix(_) => {
                    bail!("refusing unsafe repository filename: {}", path);
                }
            }
        }

        Ok(())
    }

    fn destination_is_usable(
        destination: &Path,
        source: &Path,
    ) -> Result<bool> {
        if !destination.exists() {
            return Ok(false);
        }

        let destination_size = fs::metadata(destination)
            .with_context(|| {
                format!(
                    "failed to inspect existing destination {:?}",
                    destination
                )
            })?
            .len();

        let source_size = fs::metadata(source)
            .with_context(|| {
                format!(
                    "failed to inspect cached source {:?}",
                    source
                )
            })?
            .len();

        // Zero-byte files are always considered broken.
        if destination_size == 0 {
            return Ok(false);
        }

        // Size mismatch means the destination is stale/incomplete.
        if destination_size != source_size {
            return Ok(false);
        }

        Ok(true)
    }

    fn atomic_install(&self, source: &Path, destination: &Path) -> Result<()> {
        if let Some(parent) = destination.parent() {
            fs::create_dir_all(parent)
                .with_context(|| {
                    format!("failed to create directory {:?}", parent)
                })?;
        }

        let file_name = destination
            .file_name()
            .and_then(|name| name.to_str())
            .context("destination filename is not valid UTF-8")?;

        let temporary_name = format!(
            ".{}.part-{}",
            file_name,
            std::process::id()
        );

        let temporary_path = destination
            .parent()
            .unwrap_or_else(|| Path::new("."))
            .join(temporary_name);

        // A stale temporary file from a previous crash must never be reused.
        let _ = fs::remove_file(&temporary_path);

        fs::copy(source, &temporary_path)
            .with_context(|| {
                format!(
                    "failed copying cached file {:?} -> {:?}",
                    source,
                    temporary_path
                )
            })?;

        let copied_size = fs::metadata(&temporary_path)
            .with_context(|| {
                format!(
                    "failed to inspect temporary file {:?}",
                    temporary_path
                )
            })?
            .len();

        let source_size = fs::metadata(source)
            .with_context(|| {
                format!("failed to inspect source {:?}", source)
            })?
            .len();

        if copied_size == 0 || copied_size != source_size {
            let _ = fs::remove_file(&temporary_path);

            bail!(
                "temporary copy size mismatch for {}: copied={} expected={}",
                file_name,
                copied_size,
                source_size
            );
        }

        // Windows cannot replace an existing file with rename().
        // Remove an invalid/stale destination only after the complete temporary
        // copy has succeeded.
        if destination.exists() {
            fs::remove_file(destination)
                .with_context(|| {
                    format!("failed removing stale destination {:?}", destination)
                })?;
        }

        if let Err(error) = fs::rename(&temporary_path, destination) {
            let _ = fs::remove_file(&temporary_path);

            return Err(error).with_context(|| {
                format!(
                    "failed atomically installing {:?} -> {:?}",
                    temporary_path,
                    destination
                )
            });
        }

        Ok(())
    }

    fn sync_one(
        &self,
        repo: &hf_hub::api::sync::ApiRepo,
        repository_filename: &str,
        destination_root: &Path,
    ) -> Result<()> {
        Self::validate_relative_repo_path(repository_filename)?;

        let destination = destination_root.join(repository_filename);

        let cached_path = self.retry(
            &format!("download {}", repository_filename),
            || {
                repo.get(repository_filename)
                    .with_context(|| {
                        format!(
                            "failed downloading repository file {}",
                            repository_filename
                        )
                    })
            },
        )?;

        let source_size = fs::metadata(&cached_path)
            .with_context(|| {
                format!(
                    "failed reading cached file metadata {:?}",
                    cached_path
                )
            })?
            .len();

        if source_size == 0 {
            bail!(
                "Hugging Face returned an empty file for {}",
                repository_filename
            );
        }

        if Self::destination_is_usable(&destination, &cached_path)? {
            println!(
                "⏭️ Up to date: {}",
                repository_filename
            );

            return Ok(());
        }

        println!(
            "📥 Installing: {} ({:.2} MB)",
            repository_filename,
            source_size as f64 / 1024.0 / 1024.0
        );

        self.atomic_install(&cached_path, &destination)?;

        let installed_size = fs::metadata(&destination)
            .with_context(|| {
                format!(
                    "failed inspecting installed file {:?}",
                    destination
                )
            })?
            .len();

        if installed_size != source_size {
            bail!(
                "installed file verification failed for {}: {} != {}",
                repository_filename,
                installed_size,
                source_size
            );
        }

        Ok(())
    }

    fn sync_exact_files(
        &self,
        repo_id: &str,
        destination_dir: impl AsRef<Path>,
        required_files: &[&str],
    ) -> Result<()> {
        let destination_dir = destination_dir.as_ref();

        fs::create_dir_all(destination_dir)
            .with_context(|| {
                format!(
                    "failed to create output directory {:?}",
                    destination_dir
                )
            })?;

        println!();
        println!("📦 Repository: {}", repo_id);

        let repo = self.api.model(repo_id.to_owned());

        let repo_info = self.retry(
            &format!("repository metadata lookup for {}", repo_id),
            || {
                repo.info()
                    .with_context(|| {
                        format!(
                            "failed looking up repository metadata for {}",
                            repo_id
                        )
                    })
            },
        )?;

        // Validate the whole manifest before modifying any destination files.
        //
        // This prevents a typo in the manifest from leaving you with a partially
        // installed model before the actual failure becomes visible.
        for required in required_files {
            if !repo_info
                .siblings
                .iter()
                .any(|sibling| sibling.rfilename == *required)
            {
                bail!(
                    "required file '{}' does not exist in Hugging Face repository '{}'",
                    required,
                    repo_id
                );
            }
        }

        for required in required_files {
            self.sync_one(
                &repo,
                required,
                destination_dir,
            )?;
        }

        println!(
            "✅ Repository ready: {}",
            destination_dir.display()
        );

        Ok(())
    }
}

// =============================================================================
// Environment / diagnostics
// =============================================================================

fn print_selected_model(config: ModelConfig) {
    println!();
    println!("🧠 Selected Qwen model");
    println!("   Repository : {}", config.repo_id);
    println!("   Parameters : {}", config.param_size);
    println!("   Quant      : {}", config.quantization);
    println!("   Files      : {}", config.files.len());

    for file in config.files {
        println!("                - {}", file);
    }
}

fn validate_output_roots() -> Result<()> {
    for path in [
        QWEN_OUTPUT_DIR,
        BGE_OUTPUT_DIR,
    ] {
        if Path::new(path).components().any(|component| {
            matches!(component, Component::ParentDir)
        }) {
            bail!("unsafe output directory configured: {}", path);
        }
    }

    Ok(())
}

// =============================================================================
// Main
// =============================================================================

fn main() -> Result<()> {
    println!("==============================================================");
    println!(" FinTally Model Provisioner");
    println!(" Production Hardware-Adaptive Hugging Face Synchronization");
    println!("==============================================================");

    validate_output_roots()
        .context("output path validation failed")?;

    // -------------------------------------------------------------------------
    // 1. Hardware detection
    // -------------------------------------------------------------------------

    let hardware = detect_hardware();

    println!();
    println!("🔍 Hardware detection");

    match hardware.ram_gb() {
        Some(ram) => println!("   RAM  : {:.1} GB", ram),
        None => println!("   RAM  : unknown"),
    }

    match hardware.gpu {
        Some(gpu) => {
            println!("   GPU  : NVIDIA");
            println!("   VRAM : {:.1} GB", gpu.vram_gb());

            match gpu.power_limit_watts {
                Some(power) => println!("   TGP  : {:.0} W", power),
                None => println!(
                    "   TGP  : unavailable from NVIDIA driver (treated as unknown)"
                ),
            }
        }

        None => {
            println!("   GPU  : NVIDIA GPU not detected");
        }
    }

    // -------------------------------------------------------------------------
    // 2. Build HF client once
    // -------------------------------------------------------------------------

    let downloader = HuggingFaceDownloader::new()
        .context("failed to initialize model downloader")?;

    // -------------------------------------------------------------------------
    // 3. Dynamic Qwen selection
    // -------------------------------------------------------------------------

    let qwen_config = select_optimal_qwen_model(hardware);

    print_selected_model(qwen_config);

    downloader
        .sync_exact_files(
            qwen_config.repo_id,
            QWEN_OUTPUT_DIR,
            qwen_config.files,
        )
        .context("Qwen model synchronization failed")?;

    // -------------------------------------------------------------------------
    // 4. BGE Small
    // -------------------------------------------------------------------------
    //
    // Only production inference artifacts are downloaded.
    //
    // We intentionally do NOT download pytorch_model.bin because:
    // - model.safetensors is already available
    // - the .bin file is unnecessary duplication
    // - avoiding pickle-based artifacts reduces the deployment attack surface

    downloader
        .sync_exact_files(
            "BAAI/bge-small-en-v1.5",
            BGE_OUTPUT_DIR,
            BGE_FILES,
        )
        .context("BGE model synchronization failed")?;

    // -------------------------------------------------------------------------
    // 5. Final validation
    // -------------------------------------------------------------------------

    println!();
    println!("🔎 Final artifact validation");

    for directory in [
        QWEN_OUTPUT_DIR,
        BGE_OUTPUT_DIR,
    ] {
        let path = Path::new(directory);

        if !path.exists() {
            bail!(
                "expected model directory does not exist: {}",
                directory
            );
        }

        println!("   ✅ {}", path.display());
    }

    println!();
    println!("🎉 FinTally model provisioning completed successfully.");
    println!("   Models are installed using atomic file replacement.");
    println!("   Incomplete/zero-byte artifacts will be re-synchronized.");
    println!("   Repository manifests are validated before installation.");
    println!("==============================================================");

    Ok(())
}
