use std::fs::File;
use std::io::Read;
use std::path::Path;
use crate::core::rag::errors::RagError;
use super::documents::RawDocument;

pub struct DocumentLoader;

impl DocumentLoader {
    pub fn load_from_file<P: AsRef<Path>>(path: P) -> Result<RawDocument, RagError> {
        let path_ref = path.as_ref();
        
        // 1. Allocate file_name directly as an exact-size Box<str>
        let file_name: Box<str> = path_ref
            .file_name()
            .and_then(|os_str| os_str.to_str())
            .unwrap_or("unknown_source")
            .into();

        // 2. Query file size to make exactly ONE allocation for reading
        let mut file = File::open(path_ref).map_err(RagError::IoError)?;
        let file_size = file.metadata().map(|m| m.len() as usize).unwrap_or(0);
        
        let mut raw_content = String::with_capacity(file_size);
        file.read_to_string(&mut raw_content).map_err(RagError::IoError)?;

        // 3. SINGLE-PASS LAZY WHITESPACE NORMALIZATION
        // This eliminates the massive intermediate Vec<&str> allocation.
        let mut normalized_content = String::with_capacity(raw_content.len());
        let mut words = raw_content.split_whitespace(); // Lazy iterator, zero allocation
        
        if let Some(first_word) = words.next() {
            normalized_content.push_str(first_word);
            for word in words {
                normalized_content.push(' ');
                normalized_content.push_str(word);
            }
        }
        
        // Explicitly drop raw_content here to reclaim memory before generating metadata
        drop(raw_content);

        // 4. DIRECT METADATA BALANCING (No HashMap middleman)
        let file_type: Box<str> = path_ref
            .extension()
            .and_then(|ext| ext.to_str())
            .unwrap_or("txt")
            .into();

        let metadata: Box<[(Box<str>, Box<str>)]> = vec![
            ("source_file".into(), file_name.clone()),
            ("file_type".into(), file_type),
        ].into_boxed_slice();

        // 5. Build RawDocument directly without conversion steps
        Ok(RawDocument {
            id: file_name,
            content: normalized_content.into_boxed_str(),
            metadata,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::env;
    use std::path::PathBuf;

    // Helper utility to safely initialize temporary files for distinct test scopes
    fn create_temporary_file(filename: &str, raw_bytes: &str) -> PathBuf {
        let mut path = env::temp_dir();
        path.push(format!("fintally_loader_{}_{}", thread_unique_id(), filename));
        fs::write(&path, raw_bytes).unwrap();
        path
    }

    fn thread_unique_id() -> u32 {
        let mut hasher = std::collections::hash_map::DefaultHasher::new();
        std::time::Instant::now().hash(&mut hasher);
        hasher.finish() as u32
    }
    use std::hash::{Hash, Hasher};

    // ==========================================
    // 1. LAZY WHITESPACE NORMALIZATION TESTS
    // ==========================================

    #[test]
    fn test_load_collapses_erratic_whitespace_and_newlines() {
        // Formulate a messy string littered with erratic spacing, tabs, and newlines
        let chaotic_input = "  Asset   ledger   reconciliation.\n\nNew   line tracking \t token.  ";
        let temp_file = create_temporary_file("chaotic_spacing.txt", chaotic_input);

        let result = DocumentLoader::load_from_file(&temp_file);
        assert!(result.is_ok(), "Failed loading file path targets: {:?}", result.err());

        let document = result.unwrap();

        // Verify whitespace normalization compressed erratic gaps down to singular spaces
        // and completely stripped leading/trailing spaces.
        let expected_clean_text = "Asset ledger reconciliation. New line tracking token.";
        assert_eq!(document.content.as_ref(), expected_clean_text);

        let _ = fs::remove_file(temp_file);
    }

    // ==========================================
    // 2. METADATA EXTRACTION & STRUCT VERIFICATION
    // ==========================================

    #[test]
    fn test_metadata_extracts_source_name_and_extension_variants() {
        let text_content = "Isolated data string.";
        // Create a file targeting a specific custom file extension format
        let temp_file = create_temporary_file("financial_manifest.json", text_content);
        let plain_filename = temp_file.file_name().unwrap().to_str().unwrap();

        let document = DocumentLoader::load_from_file(&temp_file).unwrap();

        // 1. Verify Document ID maps directly to the filename box array
        assert_eq!(document.id.as_ref(), plain_filename);

        // 2. Scan sequential flat boxed array metadata fields
        let metadata_slice: &[(Box<str>, Box<str>)] = &document.metadata;
        assert_eq!(metadata_slice.len(), 2, "Metadata box slice size misaligned.");

        let source_meta = metadata_slice.iter().find(|(k, _)| k.as_ref() == "source_file").unwrap();
        let type_meta = metadata_slice.iter().find(|(k, _)| k.as_ref() == "file_type").unwrap();

        assert_eq!(source_meta.1.as_ref(), plain_filename);
        assert_eq!(type_meta.1.as_ref(), "json");

        let _ = fs::remove_file(temp_file);
    }

    #[test]
    fn test_fallback_metadata_defaults_for_missing_extensions() {
        let text_content = "Extensionless document text payload.";
        // Create a file without an extension dot descriptor
        let temp_file = create_temporary_file("no_extension_file", text_content);

        let document = DocumentLoader::load_from_file(&temp_file).unwrap();
        let type_meta = document.metadata.iter().find(|(k, _)| k.as_ref() == "file_type").unwrap();

        // Your unwrap_or("txt") fallback flag must kick in here smoothly
        assert_eq!(type_meta.1.as_ref(), "txt");

        let _ = fs::remove_file(temp_file);
    }

    // ==========================================
    // 3. STORAGE DISK FAULT BOUNDARIES
    // ==========================================

    #[test]
    fn test_missing_file_returns_clean_io_error() {
        // Attempting to read a file target that isn't on disk
        let non_existent_path = env::temp_dir().join("missing_fintally_loader_target.cfg");
        
        let result = DocumentLoader::load_from_file(&non_existent_path);
        assert!(result.is_err(), "Loader allowed loading missing disk file streams.");

        match result.unwrap_err() {
            RagError::IoError(err) => {
                assert_eq!(err.kind(), std::io::ErrorKind::NotFound);
            },
            other => panic!("Expected standard RagError::IoError wrapping, caught: {:?}", other),
        }
    }
}