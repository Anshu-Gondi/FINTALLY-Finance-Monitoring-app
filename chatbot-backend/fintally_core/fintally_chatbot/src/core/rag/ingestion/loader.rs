use std::collections::HashMap;
use std::fs::File;
use std::io::Read;
use std::path::Path;
use crate::core::rag::errors::RagError;
use super::documents::RawDocument;

pub struct DocumentLoader;

impl DocumentLoader {
    pub fn load_from_file<P: AsRef<Path>>(path: P) -> Result<RawDocument, RagError> {
        let path_ref = path.as_ref();
        
        let file_name = path_ref
            .file_name()
            .and_then(|os_str| os_str.to_str())
            .unwrap_or("unknown_source")
            .to_string();

        let mut file = File::open(path_ref).map_err(RagError::IoError)?;
        let mut content = String::new();
        file.read_to_string(&mut content).map_err(RagError::IoError)?;

        // Celeron optimization: Single-pass text normalization to collapse whitespace spans
        let normalized_content = content
            .split_whitespace()
            .collect::<Vec<&str>>()
            .join(" ");

        let mut metadata = HashMap::new();
        metadata.insert("source_file".to_string(), file_name.clone());
        metadata.insert(
            "file_type".to_string(),
            path_ref.extension().and_then(|ext| ext.to_str()).unwrap_or("txt").to_string(),
        );

        Ok(RawDocument::new(file_name, normalized_content, metadata))
    }
}