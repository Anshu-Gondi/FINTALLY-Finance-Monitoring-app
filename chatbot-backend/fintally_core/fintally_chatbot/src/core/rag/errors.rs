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
}

// 1. Manually implement formatting for user-facing error strings
impl fmt::Display for RagError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RagError::IngestionError(msg) => write!(f, "Document ingestion failed: {msg}"),
            RagError::ChunkingError(msg) => write!(f, "Text chunking boundary failure: {msg}"),
            RagError::EmbeddingError(msg) => write!(f, "Python embedding engine failure: {msg}"),
            RagError::VectorStoreError(msg) => write!(f, "Vector store indexing anomaly: {msg}"),
            RagError::QuantizationError(msg) => write!(f, "GGUF-style quantization out of bounds: {msg}"),
            RagError::RetrievalError(msg) => write!(f, "Information retrieval timed out: {msg}"),
            RagError::IoError(err) => write!(f, "Underlying System I/O error occurred: {err}"),
            RagError::SerializationError(msg) => write!(f, "Internal serialization/deserialization failure: {msg}"),
        }
    }
}

// 2. Register it as an official Rust Error type for standard library compatibility
impl Error for RagError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            // Expose the underlying system I/O error if it was the root cause
            RagError::IoError(err) => Some(err),
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