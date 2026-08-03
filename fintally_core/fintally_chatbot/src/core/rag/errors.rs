use std::error::Error;
use std::fmt;

#[derive(Debug)]
pub enum RagError {
    IngestionError(String),
    ChunkingError(String),
    EmbeddingError(String),
    VectorStoreError(String),
    QuantizationError(String),
    RetrievalError(String),
    IoError(std::io::Error),
    SerializationError(String),
    CandleError(candle_core::Error), // ◄─ Added candle tensor engine variant
}

// 1. Manually implement formatting for user-facing error strings
impl fmt::Display for RagError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RagError::IngestionError(msg) => write!(f, "Document ingestion failed: {msg}"),
            RagError::ChunkingError(msg) => write!(f, "Text chunking boundary failure: {msg}"),
            RagError::EmbeddingError(msg) => write!(f, "Python embedding engine failure: {msg}"),
            RagError::VectorStoreError(msg) => write!(f, "Vector store indexing anomaly: {msg}"),
            RagError::QuantizationError(msg) => {
                write!(f, "GGUF-style quantization out of bounds: {msg}")
            }
            RagError::RetrievalError(msg) => write!(f, "Information retrieval timed out: {msg}"),
            RagError::IoError(err) => write!(f, "Underlying System I/O error occurred: {err}"),
            RagError::SerializationError(msg) => {
                write!(f, "Internal serialization/deserialization failure: {msg}")
            }
            RagError::CandleError(err) => write!(f, "Candle tensor engine failure: {err}"), // ◄─ Display format
        }
    }
}

// 2. Register it as an official Rust Error type for standard library compatibility
impl Error for RagError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            RagError::IoError(err) => Some(err),
            RagError::CandleError(err) => Some(err), // ◄─ Bubble candle source up safely
            _ => None,
        }
    }
}

// 3. Auto-convert standard file I/O operations using the standard `?` operator
impl From<std::io::Error> for RagError {
    fn from(err: std::io::Error) -> Self {
        RagError::IoError(err)
    }
}

// 4. Convert USearch string errors into our typed safety enum
impl From<String> for RagError {
    fn from(err: String) -> Self {
        RagError::VectorStoreError(err)
    }
}

// 5. Auto-convert candle matrix failures into RagError via standard `?` bounds
impl From<candle_core::Error> for RagError {
    fn from(err: candle_core::Error) -> Self {
        RagError::CandleError(err)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;
    use std::io::{Error as IoError, ErrorKind};
    use std::sync::Arc;
    use std::path::Path;

    // Core workspace references
    use crate::core::rag::service::RagService;
    use crate::core::rag::ingestion::pipeline::IngestionPipeline;
    use crate::core::rag::vectorstore::usearch_store::UsearchStore;
    use crate::core::rag::vectorstore::memory_store::MemoryVectorStore;
    use crate::core::rag::embedding::generator::{EmbeddingGenerator, NativeEmbedder};
    use crate::core::rag::retrieval::retriever::VectorRetriever;

    // Target reference to your real downloaded unquantized BGE model directories
    const BGE_VAULT_PATH: &str = "../llm_models/embedding/bge_safetensors_output";

    // Dynamic test builder that constructs your complete service layers natively
    fn setup_test_service(idx_path: Option<String>, chk_path: Option<String>) -> Option<RagService> {
        if !Path::new(BGE_VAULT_PATH).exists() {
            return None; // Safely skip execution if model files aren't physically present
        }

        let embedder = Arc::new(NativeEmbedder::load_from_vault(BGE_VAULT_PATH).ok()?);

        // FIX: Match the dimensions of the BGE model instead of hardcoding `4`.
        // 384 is standard for bge-small. Use 768 if you are using bge-base.
        let test_usearch_store = UsearchStore::new(384, 100)
            .expect("Failed to initialize test USearch storage layout");

        Some(RagService::new(
            IngestionPipeline::new(512, 50),
            test_usearch_store,
            MemoryVectorStore::new(),
            EmbeddingGenerator::new(embedder),
            VectorRetriever::new(0.5),
            5,
            idx_path,
            chk_path
        ))
    }

    // ==========================================
    // 1. ERROR LAYER TESTS
    // ==========================================

    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_error_auto_conversions() {
        let standard_io_err = IoError::new(ErrorKind::PermissionDenied, "write blocked");
        let rag_err_from_io: RagError = standard_io_err.into();

        if let RagError::IoError(ref internal_err) = rag_err_from_io {
            assert_eq!(internal_err.kind(), ErrorKind::PermissionDenied);
        } else {
            panic!("Expected RagError::IoError variant");
        }

        let usearch_err_msg = "HNSW index allocation fault".to_string();
        let rag_err_from_string: RagError = usearch_err_msg.into();

        if let RagError::VectorStoreError(ref msg) = rag_err_from_string {
            assert_eq!(msg, "HNSW index allocation fault");
        } else {
            panic!("Expected RagError::VectorStoreError variant");
        }

        let candle_err = candle_core::Error::Msg("Shape mismatch during unsqueeze".to_string());
        let rag_err_from_candle: RagError = candle_err.into();
        if let RagError::CandleError(ref err) = rag_err_from_candle {
            assert!(err.to_string().contains("Shape mismatch"));
        } else {
            panic!("Expected RagError::CandleError variant");
        }
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_error_display_formatting() {
        let err = RagError::EmbeddingError("ONNX runtime uninitialized".into());
        let formatted_string = format!("{}", err);
        assert_eq!(formatted_string, "Python embedding engine failure: ONNX runtime uninitialized");

        let serialization_err = RagError::SerializationError("JSON key mismatch".into());
        assert_eq!(
            format!("{}", serialization_err),
            "Internal serialization/deserialization failure: JSON key mismatch"
        );
    }

    // ==========================================
    // 2. CORE SERVICE PIPELINE TESTS
    // ==========================================

    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_persistence_path_guards() {
        let lazy_service = match setup_test_service(None, None) {
            Some(service) => service,
            None => {
                println!("skipping test: Real bge vault path not found locally.");
                return;
            }
        };
        assert!(!lazy_service.has_persisted_data());

        let strict_service = setup_test_service(Some("idx.bin".into()), Some("chunks.json".into())).unwrap();
        assert!(strict_service.has_persisted_data());
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_query_pipeline_filter_conversion() {
        let service = match setup_test_service(None, None) {
            Some(service) => service,
            None => return,
        };

        let mut filters = BTreeMap::new();
        filters.insert("file_type".into(), "pdf".into());
        filters.insert("department".into(), "finance".into());

        let query_execution = service.query(
            "Fetch Q4 balance sheets",
            Some(3),
            Some(0.1),
            Some(filters)
        );

        assert!(
            query_execution.is_ok(),
            "Query system broke converting filter boundaries: {:?}",
            query_execution.err()
        );

        let response = query_execution.unwrap();
        let _structural_integrity_check = &response.context_block;
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_safe_error_bubbling_on_missing_disk() {
        let service = match setup_test_service(None, None) {
            Some(service) => service,
            None => return,
        };

        let execution_result = service.save_to_disk();
        assert!(execution_result.is_err());

        match execution_result.unwrap_err() {
            RagError::SerializationError(msg) => {
                assert!(msg.contains("Missing Index Dump Path Setup"));
            }
            other => {
                panic!("Expected a SerializationError but caught structural variant: {:?}", other)
            }
        }
    }
}
