use fintally_chatbot::core::vision::engine::{DocumentInput, VisionEngine};
use std::fs::{self, File};
use std::io::Write;
use std::path::Path;

/// Helper to set up mock florence2 model files in local directory structure
fn setup_mock_model_assets(model_dir: &Path) -> anyhow::Result<()> {
    fs::create_dir_all(model_dir)?;

    // 1. Create minimal PaliGemma compatible config.json
    let config_path = model_dir.join("config.json");
    let config_json = serde_json::json!({
        // Root-level fields expected by Candle's paligemma::Config
        "projection_dim": 2048,
        "hidden_size": 2048,
        "vocab_size": 257152,
        "ignore_index": -100,
        "image_token_id": 257152,
        "text_config": {
            "vocab_size": 257152,
            "hidden_size": 2048,
            "intermediate_size": 16384,
            "num_hidden_layers": 18,
            "num_attention_heads": 8,
            "num_key_value_heads": 1,
            "head_dim": 256,
            "hidden_act": "gelu_pytorch_tanh",
            "max_position_embeddings": 8192,
            "initializer_range": 0.02,
            "rms_norm_eps": 1e-6,
            "use_cache": true,
            "pad_token_id": 0,
            "eos_token_id": 1,
            "bos_token_id": 2,
            "rope_theta": 10000.0,
            "attention_bias": false
        },
        "vision_config": {
            "hidden_size": 1152,
            "intermediate_size": 4304,
            "num_hidden_layers": 27,
            "num_attention_heads": 16,
            "image_size": 224,
            "patch_size": 14,
            "projection_dim": 2048,
            "attention_bias": false,
            "num_channels": 3
        }
    });
    let mut file = File::create(&config_path)?;
    file.write_all(config_json.to_string().as_bytes())?;

    // 2. Create minimal tokenizer.json
    let tokenizer_path = model_dir.join("tokenizer.json");
    let tokenizer_json = serde_json::json!({
        "version": "1.0",
        "truncation": null,
        "padding": null,
        "added_tokens": [
            {"id": 0, "content": "<pad>", "single_word": false, "lstrip": false, "rstrip": false, "normalized": false, "special": true},
            {"id": 1, "content": "</s>", "single_word": false, "lstrip": false, "rstrip": false, "normalized": false, "special": true},
            {"id": 2, "content": "<s>", "single_word": false, "lstrip": false, "rstrip": false, "normalized": false, "special": true}
        ],
        "normalizer": null,
        "pre_tokenizer": null,
        "post_processor": null,
        "decoder": null,
        "model": {
            "type": "BPE",
            "dropout": null,
            "unk_token": null,
            "continuing_subword_prefix": null,
            "end_of_word_suffix": null,
            "fuse_unk": false,
            "vocab": {
                "<pad>": 0,
                "</s>": 1,
                "<s>": 2,
                "Extract": 3,
                "Text": 4
            },
            "merges": []
        }
    });
    let mut file = File::create(&tokenizer_path)?;
    file.write_all(tokenizer_json.to_string().as_bytes())?;

    // 3. Create mock safetensors binary file header
    let weights_path = model_dir.join("model.safetensors");
    let mut file = File::create(&weights_path)?;
    let header_str = "{}";
    let header_bytes = header_str.as_bytes();
    let header_len = header_bytes.len() as u64;

    file.write_all(&header_len.to_le_bytes())?;
    file.write_all(header_bytes)?;

    Ok(())
}

#[test]
fn test_vision_engine_creation_and_inference() {
    let mock_dir = Path::new("target/test_models/florence2_mock");
    setup_mock_model_assets(mock_dir).expect("Failed to construct mock model directory assets");

    let mut engine = VisionEngine::from_model_dir(mock_dir)
        .expect("Failed to construct VisionEngine instance from mock assets");

    let sample_bytes = vec![200u8; 1024];

    // 1. Test Raw Image Processing
    let raw_res = engine.process_input(
        DocumentInput::RawImageBytes(&sample_bytes),
        "Extract Text",
    );
    assert!(raw_res.is_ok(), "Raw image processing failed: {:?}", raw_res.err());

    // 2. Test PDF Page Processing
    let pdf_res = engine.process_input(
        DocumentInput::PdfDocumentBytes(&sample_bytes),
        "Extract Text",
    );
    assert!(pdf_res.is_ok(), "PDF page processing failed: {:?}", pdf_res.err());

    // 3. Test Financial Chart Processing
    let chart_res = engine.process_input(
        DocumentInput::FinancialChartBytes(&sample_bytes),
        "Extract Text",
    );
    assert!(chart_res.is_ok(), "Financial chart processing failed: {:?}", chart_res.err());
}
