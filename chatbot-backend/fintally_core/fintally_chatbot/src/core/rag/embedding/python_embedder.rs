use pyo3::prelude::*;
use std::sync::OnceLock;
use crate::core::rag::errors::RagError;

pub struct PythonEmbedder;

impl PythonEmbedder {
    #[inline]
    pub fn new() -> Self {
        Self
    }

    /// Passes raw text to an external python routine leveraging ONNX CPU runtimes.
    /// Uses a thread-safe OnceLock to cache the Python module lookup, reducing 
    /// subsequent lookup overhead to near-zero.
    pub fn embed_text(&self, text: &str) -> Result<Vec<f32>, RagError> {
        // Static allocation pool to hold the compiled Python module pointer globally
        static CACHED_MODULE: OnceLock<Py<PyModule>> = OnceLock::new();

        Python::with_gil(|py| {
            // 1. LAZY ONE-TIME MODULE INITIALIZATION
            // If the module is already imported, we safely ref to it instantly.
            // If not, we import it exactly once for the entire application lifecycle.
            let bound_module = if let Some(py_module_ref) = CACHED_MODULE.get() {
                py_module_ref.as_ref(py)
            } else {
                let imported_m = py.import("fintally_embedder")
                    .map_err(|e| RagError::EmbeddingError(format!(
                        "Could not load python module 'fintally_embedder.py'. Error: {e}"
                    )))?;
                
                // Save the module pointer into global memory space
                let _ = CACHED_MODULE.set(imported_m.into_py(py));
                imported_m
            };

            // 2. ZERO-TRASH EXECUTION
            // Call the ONNX extraction loop directly via the cached module reference
            let result = bound_module.call_method1("get_onnx_embedding", (text,))
                .map_err(|e| RagError::EmbeddingError(format!("ONNX execution exception: {e}")))?;

            // Extract the Python float list directly into a standard Rust allocation pool
            let vector: Vec<f32> = result.extract()
                .map_err(|e| RagError::EmbeddingError(format!("Failed to parse Python list into Vec<f32>: {e}")))?;

            Ok(vector)
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::env;
    use std::path::PathBuf;
    use pyo3::prelude::*;

    /// Generates a local mock Python module on disk to safely isolate our test environment
    // Fix 1: Pass a test_name string to prevent parallel race conditions
    fn generate_mock_python_script(test_name: &str) -> PathBuf {
        pyo3::prepare_freethreaded_python();

        let mut tmp_dir = env::temp_dir();
        // Fix 2: Append the unique suffix name here
        tmp_dir.push(format!("fintally_embedder_env_{}", test_name));
        fs::create_dir_all(&tmp_dir).unwrap();

        // Emulates the targeted ONNX runtime extraction format
        let python_code = r#"
def get_onnx_embedding(text: str):
    if not text:
        raise ValueError("ONNX execution exception: Empty input string")
    # Return a predictable dummy dimension vector
    return [0.125, -0.5, 0.875]
"#;
        let script_path = tmp_dir.join("fintally_embedder.py");
        fs::write(&script_path, python_code).unwrap();
        tmp_dir
    }

    // ==========================================
    // 1. END-TO-END EMBEDDING RESOLUTION
    // ==========================================

    #[test]
    fn test_embed_text_successful_roundtrip() {
        // Fix 3: Supply unique suffix ID string
        let test_env_dir = generate_mock_python_script("successful_roundtrip");

        // Inject our isolated temporary path directly into Python's global search directory
        Python::with_gil(|py| {
            let sys = py.import("sys").expect("Failed to boot Python sys module.");
            let path_list: &pyo3::types::PyList = sys
                .getattr("path")
                .expect("Failed to extract sys.path sequence.")
                .downcast()
                .expect("sys.path failed structural vector cast.");
            
            path_list
                .insert(0, test_env_dir.to_str().unwrap())
                .expect("Failed patching environment path.");
        });

        let embedder = PythonEmbedder::new();
        
        // Execute extraction. The first run compiles and saves into the CACHED_MODULE OnceLock frame.
        let result = embedder.embed_text("Cache prefetching optimization vector string.");
        assert!(result.is_ok(), "Embedder encountered runtime error: {:?}", result.err());

        let vector = result.unwrap();
        assert_eq!(vector.len(), 3, "Returned dimension array size misaligned.");
        assert_eq!(vector, vec![0.125, -0.5, 0.875]);

        // Secondary execution verification (Tests the rapid OnceLock pointer extraction pass)
        let cached_pass_result = embedder.embed_text("Subsequent rapid retrieval check.");
        assert!(cached_pass_result.is_ok());
        assert_eq!(cached_pass_result.unwrap(), vec![0.125, -0.5, 0.875]);

        // Clean up mock artifact footprint safely
        let _ = fs::remove_file(test_env_dir.join("fintally_embedder.py"));
        let _ = fs::remove_dir(test_env_dir);
    }

    // ==========================================
    // 2. EXCEPTION PROPAGATION BOUNDARIES
    // ==========================================

    #[test]
    fn test_embed_text_bubbles_python_exceptions_gracefully() {
        // Fix 4: Supply unique suffix ID string
        let test_env_dir = generate_mock_python_script("bubbles_exceptions");
        
        Python::with_gil(|py| {
            let sys = py.import("sys").unwrap();
            let path_list: &pyo3::types::PyList = sys.getattr("path").unwrap().downcast().unwrap();
            let _ = path_list.insert(0, test_env_dir.to_str().unwrap());
        });

        let embedder = PythonEmbedder::new();

        // Passing an empty slice triggers the mock ValueError exception logic
        let error_result = embedder.embed_text("");
        assert!(error_result.is_err(), "Pipeline accepted invalid empty text arrays without raising errors.");

        match error_result.unwrap_err() {
            RagError::EmbeddingError(error_message) => {
                assert!(
                    error_message.contains("ONNX execution exception"),
                    "Error string format failed to match runtime translation layouts. Got: {}",
                    error_message
                );
            }
            other_err => panic!("Expected specialized RagError::EmbeddingError, intercepted: {:?}", other_err),
        }

        let _ = fs::remove_file(test_env_dir.join("fintally_embedder.py"));
        let _ = fs::remove_dir(test_env_dir);
    }
}