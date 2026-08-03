use anyhow::{Context, Result};
use hf_hub::api::sync::ApiBuilder;
use std::env;
use std::fs;
use std::path::Path;

fn download_hf_repo(
    repo_id: &str,
    target_path_str: &str,
    target_file_filter: Option<&str>,
) -> Result<()> {
    println!("\n📦 Connection initialized for: {}", repo_id);

    // Read your custom environment variable name for the Hugging Face token
    let token = env::var("hugging_face_read_only_key")
        .or_else(|_| env::var("HF_TOKEN"))
        .ok();

    // Set up HF API with progress bar and optional token
    let mut api_builder = ApiBuilder::new().with_progress(true);

    if let Some(t) = token {
        api_builder = api_builder.with_token(Some(t));
    }

    let api = api_builder
        .build()
        .context("Failed to initialize Hugging Face client")?;

    let repo = api.model(repo_id.to_string());
    let repo_info = repo.info().context("Failed to look up repository information")?;

    let out_path = Path::new(target_path_str);
    fs::create_dir_all(out_path)?;

    for sibling in repo_info.siblings {
        let filename = &sibling.rfilename;

        // Match files based on target filter or default safe asset extensions
        let is_matched = match target_file_filter {
            Some(filter) => filename.eq_ignore_ascii_case(filter)
                || filename.ends_with(".json")
                || filename.ends_with(".txt")
                || filename.starts_with("tokenizer"),
            None => filename.ends_with(".safetensors")
                || filename.ends_with(".json")
                || filename.ends_with(".txt")
                || filename.starts_with("tokenizer")
                || filename.ends_with(".model"),
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

            // Retry loop to handle transient drops
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
                            return Err(e).context(format!(
                                "Download failed after retries for file: {}",
                                filename
                            ));
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

fn main() -> Result<()> {
    println!("🏁 Direct Rust pipeline starting up lightweight context mirrors...");

    let qwen_target_dir = "llm_models/chat/qwen_safetensors_output";

    // Task 1: Download Qwen 2.5 1.5B Instruct Q8_0 GGUF variant
    download_hf_repo(
        "Qwen/Qwen2.5-1.5B-Instruct-GGUF",
        qwen_target_dir,
        Some("qwen2.5-1.5b-instruct-q8_0.gguf"),
    )?;

    // Task 2: Download BGE Small Embeddings
    download_hf_repo(
        "BAAI/bge-small-en-v1.5",
        "llm_models/embedding/bge_safetensors_output",
        None,
    )?;

    // Task 3: Download Lightweight Vision-Language Model (Microsoft Florence-2-base)
    // At ~0.23B parameters, this is ultra-lightweight for T4 GPUs and handles high-speed OCR/chart parse.
    let florence_target_dir = "llm_models/ocr/florence2_base_output";
    download_hf_repo(
        "microsoft/Florence-2-base",
        florence_target_dir,
        None,
    )?;

    println!("\n🎉 Pipeline complete! Lightweight T4-optimized model binaries ready.");
    Ok(())
}
