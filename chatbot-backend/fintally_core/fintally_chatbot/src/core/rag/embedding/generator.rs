use crate::core::rag::errors::RagError;
use super::python_embedder::PythonEmbedder;
use super::types::ChunkEmbedding;

pub struct EmbeddingGenerator {
    embedder: PythonEmbedder,
}

impl EmbeddingGenerator {
    #[inline]
    pub fn new() -> Self {
        Self {
            embedder: PythonEmbedder::new(),
        }
    }

    /// Generates embeddings and packages them directly into an exact-sized chunk wrapper
    #[inline]
    pub fn generate(&self, chunk_id: u64, text: &str) -> Result<ChunkEmbedding, RagError> {
        let vector = self.embedder.embed_text(text)?;
        
        // ChunkEmbedding::new automatically handles converting the vector to a Box<[f32]>
        Ok(ChunkEmbedding::new(chunk_id, vector))
    }
}


#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::env;
    use std::path::PathBuf;
    use pyo3::prelude::*;

    /// Generates a unique local mock Python module on disk to safely isolate our test environment
    fn setup_mock_python_environment(test_name: &str) -> PathBuf {
        pyo3::prepare_freethreaded_python();

        let mut tmp_dir = env::temp_dir();
        // Fix 1: Append the unique test name so parallel tests don't share or delete the same folder
        tmp_dir.push(format!("fintally_generator_env_{}", test_name));
        fs::create_dir_all(&tmp_dir).unwrap();

        // Fix 2: Keep the message synchronized with what your assertions look for
        let python_code = r#"
def get_onnx_embedding(text: str):
    if not text:
        raise ValueError("ONNX execution exception: Empty text buffer")
    return [0.5, -0.25, 0.75, 1.0]
"#;
        let script_path = tmp_dir.join("fintally_embedder.py");
        fs::write(&script_path, python_code).unwrap();
        tmp_dir
    }

    // ==========================================
    // 1. END-TO-END GENERATION & BOXING
    // ==========================================

    #[test]
    fn test_embedding_generation_packages_into_boxed_slice() {
        let test_env_dir = setup_mock_python_environment("boxed_slice");

        // Inject the isolated temporary path directly into Python's search path
        Python::with_gil(|py| {
            let sys = py.import("sys").expect("Failed to boot Python sys module.");
            let path_list: &pyo3::types::PyList = sys
                .getattr("path")
                .expect("Failed to extract sys.path sequence.")
                .downcast()
                .expect("sys.path failed structural vector cast.");
            
            let _ = path_list.insert(0, test_env_dir.to_str().unwrap());
        });

        let generator = EmbeddingGenerator::new();
        let target_chunk_id = 42u64;
        
        // Execute the top-level generation framework
        let result = generator.generate(target_chunk_id, "Optimized data pipeline execution chunk.");
        assert!(result.is_ok(), "Embedding generation failed: {:?}", result.err());

        let chunk_embedding = result.unwrap();

        // 1. Verify IDs mapped correctly
        assert_eq!(chunk_embedding.chunk_id, target_chunk_id);

        // 2. Verify dimension scaling and exact value passing
        assert_eq!(chunk_embedding.dimension_size(), 4);
        assert_eq!(chunk_embedding.vector.as_ref(), &[0.5, -0.25, 0.75, 1.0]);

        // Clean up environment footprint safely
        let _ = fs::remove_file(test_env_dir.join("fintally_embedder.py"));
        let _ = fs::remove_dir(test_env_dir);
    }

    // ==========================================
    // 2. UNDERLYING FAULT PROPAGATION
    // ==========================================

    #[test]
    fn test_generation_bubbles_underlying_embedder_errors() {
        let test_env_dir = setup_mock_python_environment("bubbles_errors");
        
        Python::with_gil(|py| {
            let sys = py.import("sys").unwrap();
            let path_list: &pyo3::types::PyList = sys.getattr("path").unwrap().downcast().unwrap();
            let _ = path_list.insert(0, test_env_dir.to_str().unwrap());
        });

        let generator = EmbeddingGenerator::new();

        // Passing an empty string triggers the mock's ValueError exception logic
        let error_result = generator.generate(101, "");
        assert!(error_result.is_err(), "Generator accepted empty values without bubbling exceptions.");

        match error_result.unwrap_err() {
            RagError::EmbeddingError(msg) => {
                assert!(
                    msg.contains("ONNX execution exception"),
                    "Error string did not originate from expected Python embedder path. Got: {}",
                    msg
                );
            }
            other => panic!("Expected RagError::EmbeddingError wrapper, caught: {:?}", other),
        }

        // Clean up environment footprint safely
        let _ = fs::remove_file(test_env_dir.join("fintally_embedder.py"));
        let _ = fs::remove_dir(test_env_dir);
    }
}