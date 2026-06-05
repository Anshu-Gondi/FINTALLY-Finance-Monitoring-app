use std::fs::File;
use std::io::{Write, Read, BufWriter, BufReader};
use std::path::Path;
use crate::core::rag::errors::RagError;
use crate::core::rag::embedding::types::DocumentChunk;

pub struct StorePersistence;

impl StorePersistence {
    /// Serializes a collection of document chunks out to a clean system binary payload 
    pub fn save_chunks_to_disk<P: AsRef<Path>>(path: P, chunks: &[DocumentChunk]) -> Result<(), RagError> {
        let file = File::create(path).map_err(RagError::IoError)?;
        let mut writer = BufWriter::new(file);

        // Convert metadata maps array safely via JSON serialization text markers
        let serialized = serde_json::to_vec(chunks)
            .map_err(|e| RagError::SerializationError(e.to_string()))?;
            
        writer.write_all(&serialized).map_err(RagError::IoError)?;
        writer.flush().map_err(RagError::IoError)?;
        Ok(())
    }

    /// Reads raw system records back into memory elements layout arrays
    pub fn load_chunks_from_disk<P: AsRef<Path>>(path: P) -> Result<Vec<DocumentChunk>, RagError> {
        let file = File::open(path).map_err(RagError::IoError)?;
        let mut reader = BufReader::new(file);
        let mut buffer = Vec::new();
        
        reader.read_to_end(&mut buffer).map_err(RagError::IoError)?;
        
        let chunks: Vec<DocumentChunk> = serde_json::from_slice(&buffer)
            .map_err(|e| RagError::SerializationError(e.to_string()))?;
            
        Ok(chunks)
    }
}