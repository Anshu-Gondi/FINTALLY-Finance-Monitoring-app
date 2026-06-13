use std::fs::File;
use std::io::{ BufWriter, BufReader };
use std::path::Path;
use crate::core::rag::errors::RagError;
use crate::core::rag::embedding::types::DocumentChunk;

pub struct StorePersistence;

impl StorePersistence {
    /// Serializes a collection of document chunks out to disk.
    /// Optimized to stream data directly, ensuring zero intermediate heap spikes.
    pub fn save_chunks_to_disk<P: AsRef<Path>>(
        path: P,
        chunks: &[DocumentChunk]
    ) -> Result<(), RagError> {
        let file = File::create(path).map_err(RagError::IoError)?;
        let writer = BufWriter::new(file);

        // CELERON TUNING: Stream directly into the BufWriter.
        // serde_json writes data in chunks matching the internal buffer size (typically 8KB),
        // completely bypassing the need to allocate a massive intermediate Vec<u8> for the whole file.
        serde_json
            ::to_writer(writer, chunks)
            .map_err(|e| RagError::SerializationError(e.to_string()))?;

        Ok(()) // BufWriter automatically flushes its remaining internal buffer when dropped
    }

    /// Reads system records back into memory elements layout arrays using zero-copy streaming.
    pub fn load_chunks_from_disk<P: AsRef<Path>>(path: P) -> Result<Vec<DocumentChunk>, RagError> {
        let file = File::open(path).map_err(RagError::IoError)?;
        let reader = BufReader::new(file);

        // Stream tokens straight out of the file buffer directly into the target Vec.
        // This eliminates the massive intermediate allocation spike caused by read_to_end().
        let chunks: Vec<DocumentChunk> = serde_json
            ::from_reader(reader)
            .map_err(|e| RagError::SerializationError(e.to_string()))?;

        Ok(chunks)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::env;

    // Helper function to generate a unique, clean path in the system's temporary directory
    fn get_temp_file_path(filename: &str) -> std::path::PathBuf {
        let mut path = env::temp_dir();
        // Append a unique suffix to prevent parallel tests from colliding
        path.push(format!("fintally_test_{}_{}", thread_rng_id(), filename));
        path
    }

    // Small unique identifier helper to safely run tests in parallel without dependency weight
    fn thread_rng_id() -> u32 {
        std::collections::hash_map::RandomState::new().build_hasher().finish() as u32
    }
    use std::hash::{ BuildHasher, Hasher };

    // ==========================================
    // 1. STREAMING ROUNDTRIP TESTING
    // ==========================================

    #[test]
    fn test_chunks_serialization_and_deserialization_roundtrip() {
        let test_path = get_temp_file_path("chunks_roundtrip.json");

        // Mocking sample operational chunks.
        // NOTE: If your DocumentChunk struct fields differ, adjust these initializers to match!
        let mock_chunks = vec![
            DocumentChunk {
                id: 101, // Replacing chunk_id
                document_id: "doc_001".into(),
                sequence_index: 0,
                text: "Financial ledger balance sheets for Q3 matching internal records.".into(), // .into() converts String to Box<str>
                metadata: Box::from([]), // Replacing BTreeMap if that's your new type
            },
            DocumentChunk {
                id: 102,
                document_id: "doc_001".into(),
                sequence_index: 1,
                text: "Amortization table margins computed via baseline macro equations.".into(),
                metadata: Box::from([]),
            }
        ];

        // 1. Serialize out to disk via streamed BufWriter
        let save_result = StorePersistence::save_chunks_to_disk(&test_path, &mock_chunks);
        assert!(
            save_result.is_ok(),
            "Failed streaming chunk collection to disk: {:?}",
            save_result.err()
        );

        // Confirm the file actually exists on the disk layout
        assert!(test_path.exists(), "Target file layout missing from the file system tier");

        // 2. Deserialize back into memory layout using zero-copy stream processing
        let load_result = StorePersistence::load_chunks_from_disk(&test_path);
        assert!(
            load_result.is_ok(),
            "Failed reading chunk data records back from disk: {:?}",
            load_result.err()
        );

        let restored_chunks = load_result.unwrap();
        assert_eq!(restored_chunks.len(), mock_chunks.len());
        assert_eq!(restored_chunks[0].id, mock_chunks[0].id); // Updated from chunk_id to id
        assert_eq!(restored_chunks[0].document_id, mock_chunks[0].document_id); // Ensure this matches
        assert_eq!(restored_chunks[1].text, mock_chunks[1].text);

        // Safe cleanup workspace removal
        let _ = fs::remove_file(test_path);
    }

    // ==========================================
    // 2. DISK IO AND CORRUPTION GUARDRAILS
    // ==========================================

    #[test]
    fn test_missing_file_bubbles_clean_io_error() {
        let nonexistent_path = get_temp_file_path("missing_target_file.json");

        // Attempting to read a file that was never written must fail gracefully
        let result = StorePersistence::load_chunks_from_disk(&nonexistent_path);
        assert!(
            result.is_err(),
            "Pipeline allowed reading from a cold non-existent disk path target"
        );

        match result.unwrap_err() {
            RagError::IoError(err) => {
                assert_eq!(err.kind(), std::io::ErrorKind::NotFound);
            }
            other => panic!("Expected a structural RagError::IoError mapping, caught: {:?}", other),
        }
    }

    #[test]
    fn test_corrupted_json_bubbles_serialization_error() {
        let corrupt_path = get_temp_file_path("corrupt_payload.json");

        // Force write broken raw text onto the disk workspace
        let broken_json_payload =
            "{ \"chunk_id\": 42, \"text\": \"Unclosed bracket layout string tokens... ";
        fs::write(&corrupt_path, broken_json_payload).unwrap();

        // Attempting to parse bad text into structured schema should return a SerializationError
        let result = StorePersistence::load_chunks_from_disk(&corrupt_path);
        assert!(
            result.is_err(),
            "Zero-copy reader bypassed corrupted format token blocks without complaining"
        );

        match result.unwrap_err() {
            RagError::SerializationError(msg) => {
                // Ensure the underlying Serde parser messages are captured inside our domain enum
                assert!(
                    msg.contains("EOF") ||
                        msg.contains("expected") ||
                        msg.contains("control character")
                );
            }
            other =>
                panic!(
                    "Expected clean custom SerializationError variant conversion, caught: {:?}",
                    other
                ),
        }

        // Safe workspace cleanup step
        let _ = fs::remove_file(corrupt_path);
    }
}
