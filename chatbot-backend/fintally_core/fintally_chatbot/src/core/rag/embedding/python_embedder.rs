use pyo3::prelude::*;
use crate::core::rag::errors::RagError;

pub struct PythonEmbedder;

impl PythonEmbedder {
    pub fn new() -> Self {
        Self
    }

    /// Passes raw text to an external python routine leveraging ONNX CPU runtimes
    pub fn embed_text(&self, text: &str) -> Result<Vec<f32>, RagError> {
        Python::with_gil(|py| {
            // Ensure python can find modules in the current directory execution paths
            let sys = py.import("sys")
                .map_err(|e| RagError::EmbeddingError(format!("Failed to load Python sys module: {e}")))?;
            
            let path = sys.getattr("path")
                .map_err(|e| RagError::EmbeddingError(format!("Could not read sys.path: {e}")))?;

            let embedder_module = py.import("fintally_embedder")
                .map_err(|e| RagError::EmbeddingError(format!(
                    "Could not load python module 'fintally_embedder.py'. Ensure it exists in your script path. Error: {e}"
                )))?;
            
            let result = embedder_module.call_method1("get_onnx_embedding", (text,))
                .map_err(|e| RagError::EmbeddingError(format!("ONNX execution exception: {e}")))?;
            
            let vector: Vec<f32> = result.extract()
                .map_err(|e| RagError::EmbeddingError(format!("Failed to parse Python list into Vec<f32>: {e}")))?;
            
            Ok(vector)
        })
    }
}