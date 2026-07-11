use anyhow::{Context, Result};
use hf_hub::api::sync::ApiBuilder;
use std::fs;
use std::path::Path;

fn download_hf_repo(repo_id: &str, target_path_str: &str) -> Result<()> {
    println!("\n📦 Connection initialized for: {}", repo_id);
    
    // 1. Set up the HF API client with an active console progress bar
    let api = ApiBuilder::new()
        .with_progress(true)
        .build()
        .context("Failed to initialize Hugging Face client")?;
        
    let repo = api.model(repo_id.to_string());
    
    // 2. Query remote files listing
    let repo_info = repo.info().context("Failed to look up repository information")?;
    
    let out_path = Path::new(target_path_str);
    fs::create_dir_all(out_path)?;

    // 3. Match patterns: ["*.safetensors", "*.json", "*.txt", "tokenizer*"]
    for sibling in repo_info.siblings {
        let filename = &sibling.rfilename;
        
        let is_matched = filename.ends_with(".safetensors")
            || filename.ends_with(".json")
            || filename.ends_with(".txt")
            || filename.starts_with("tokenizer");

        if is_matched {
            println!("📥 Syncing file: {}", filename);
            
            // This safely pulls files through local computer cache (~/.cache/huggingface)
            let cached_path = repo.get(filename)
                .context(format!("Download interrupted for file: {}", filename))?;
            
            // Setup target layout, handling nested configurations cleanly (like 1_Pooling/config.json)
            let destination = out_path.join(filename);
            if let Some(parent) = destination.parent() {
                fs::create_dir_all(parent)?;
            }
            
            // Secure copy file over to your project folder
            fs::copy(&cached_path, &destination)
                .context(format!("Failed copying file over to destination path: {:?}", destination))?;
        }
    }
    
    println!("✅ Extraction complete. Destination ready: {}", target_path_str);
    Ok(())
}

fn main() -> Result<()> {
    println!("🏁 Direct Rust pipeline starting up context mirrors...");

    // Task 1: Download Qwen 2.5 Chat Model to your exact folder location
    download_hf_repo(
        "Qwen/Qwen2.5-1.5B-Instruct", 
        "llm_models/chat/qwen_safetensors_output"
    )?;
    
    // Task 2: Download BGE Small Embeddings to your exact folder location
    download_hf_repo(
        "BAAI/bge-small-en-v1.5", 
        "llm_models/embedding/bge_safetensors_output"
    )?;
    
    println!("\n🎉 Pipeline complete! All unquantized models are securely indexed inside your workspace.");
    Ok(())
}