use anyhow::{Context, Result};
use hf_hub::api::sync::ApiBuilder;
use std::fs;
use std::path::Path;

fn download_hf_repo(
    repo_id: &str,
    target_path_str: &str,
    target_gguf_file: Option<&str>,
) -> Result<()> {
    println!("\n📦 Connection initialized for: {}", repo_id);

    // Set up HF API with progress bar and retry allowance
    let api = ApiBuilder::new()
        .with_progress(true)
        .build()
        .context("Failed to initialize Hugging Face client")?;

    let repo = api.model(repo_id.to_string());
    let repo_info = repo.info().context("Failed to look up repository information")?;

    let out_path = Path::new(target_path_str);
    fs::create_dir_all(out_path)?;

    for sibling in repo_info.siblings {
        let filename = &sibling.rfilename;

        // Match case-insensitively for the requested GGUF target file
        let is_matched = match target_gguf_file {
            Some(gguf_target) => {
                filename.eq_ignore_ascii_case(gguf_target)
                    || filename.ends_with(".json")
                    || filename.ends_with(".txt")
                    || filename.starts_with("tokenizer")
            }
            None => {
                filename.ends_with(".safetensors")
                    || filename.ends_with(".json")
                    || filename.ends_with(".txt")
                    || filename.starts_with("tokenizer")
            }
        };

        if is_matched {
            let destination = out_path.join(filename);

            // Skip existing, complete local files
            if destination.exists() {
                if let Ok(metadata) = fs::metadata(&destination) {
                    if metadata.len() > 0 {
                        println!("⏭️ File already exists, skipping: {}", filename);
                        continue;
                    }
                }
            }

            println!("📥 Syncing file: {}", filename);

            // Retry loop to handle transient "Peer disconnected" drops
            let mut retries = 3;
            let mut cached_path = None;

            while retries > 0 {
                match repo.get(filename) {
                    Ok(path) => {
                        cached_path = Some(path);
                        break;
                    }
                    Err(e) => {
                        retries -= 1;
                        if retries == 0 {
                            return Err(e).context(format!("Download failed after retries for file: {}", filename));
                        }
                        println!("⚠️ Network glitch ({e}). Retrying ({retries} attempts left)...");
                        std::thread::sleep(std::time::Duration::from_secs(2));
                    }
                }
            }

            let cached_path = cached_path.unwrap();

            if let Some(parent) = destination.parent() {
                fs::create_dir_all(parent)?;
            }

            fs::copy(&cached_path, &destination).context(format!(
                "Failed copying file over to destination path: {:?}",
                destination
            ))?;
        }
    }

    println!("✅ Extraction complete. Destination ready: {}", target_path_str);
    Ok(())
}

fn ensure_tokenizer_files(fallback_repo_id: &str, target_path_str: &str) -> Result<()> {
    let out_path = Path::new(target_path_str);
    let tokenizer_path = out_path.join("tokenizer.json");

    if !tokenizer_path.exists() {
        println!(
            "\n🔍 `tokenizer.json` missing in GGUF repo. Fetching tokenizers from base repo: {}",
            fallback_repo_id
        );
        download_hf_repo(fallback_repo_id, target_path_str, None)?;
    }

    Ok(())
}

fn main() -> Result<()> {
    println!("🏁 Direct Rust pipeline starting up context mirrors...");

    let qwen_target_dir = "llm_models/chat/qwen_safetensors_output";

    // Task 1: Download Qwen 2.5 1.5B Instruct Q8_0 GGUF variant (Case-insensitive check handles uppercase filenames automatically)
    download_hf_repo(
        "Qwen/Qwen2.5-1.5B-Instruct-GGUF",
        qwen_target_dir,
        Some("qwen2.5-1.5b-instruct-q8_0.gguf"),
    )?;

    // Fallback check for missing tokenizer files
    ensure_tokenizer_files("Qwen/Qwen2.5-1.5B-Instruct", qwen_target_dir)?;

    // Task 2: Download BGE Small Embeddings
    download_hf_repo(
        "BAAI/bge-small-en-v1.5",
        "llm_models/embedding/bge_safetensors_output",
        None,
    )?;

    println!("\n🎉 Pipeline complete! All model binaries are securely stored.");
    Ok(())
}
