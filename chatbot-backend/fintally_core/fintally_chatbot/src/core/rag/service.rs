use std::collections::{BTreeMap, HashMap};
use std::time::Instant;
use std::path::Path;

use crate::core::rag::errors::RagError;
use crate::core::rag::dto::{IngestionResponse, QueryResponse, TextChunkMatch};
use crate::core::rag::ingestion::IngestionPipeline;
use crate::core::rag::vectorstore::{UsearchStore, MemoryVectorStore, StorePersistence};
use crate::core::rag::embedding::EmbeddingGenerator;
use crate::core::rag::retrieval::VectorRetriever;
use crate::core::rag::context::ContextFormatter;

pub struct RagService {
    pipeline: IngestionPipeline,
    vector_store: UsearchStore,
    memory_store: MemoryVectorStore,
    generator: EmbeddingGenerator,
    retriever: VectorRetriever,
    default_top_k: usize,
    pub(crate) index_path: Option<String>,
    pub(crate) chunks_path: Option<String>,
}

impl RagService {
    pub fn new(
        pipeline: IngestionPipeline,
        vector_store: UsearchStore,
        memory_store: MemoryVectorStore,
        generator: EmbeddingGenerator,
        retriever: VectorRetriever,
        default_top_k: usize,
        index_path: Option<String>,
        chunks_path: Option<String>,
    ) -> Self {
        Self {
            pipeline,
            vector_store,
            memory_store,
            generator,
            retriever,
            default_top_k,
            index_path,
            chunks_path,
        }
    }

    pub fn has_persisted_data(&self) -> bool {
        self.index_path.is_some() && self.chunks_path.is_some()
    }

    pub fn ingest_file(&mut self, file_path: &str) -> Result<IngestionResponse, RagError> {
        let start_time = Instant::now();
        let chunks = self.pipeline.process_file(file_path)?;
        let chunks_count = chunks.len();
        
        if chunks.is_empty() {
            return Ok(IngestionResponse {
                document_id: file_path.into(),
                chunks_count: 0,
                success: false,
                execution_time_ms: start_time.elapsed().as_millis() as u64,
            });
        }

        let document_id: Box<str> = chunks[0].document_id.clone().into();

        for chunk in chunks {
            let chunk_id = chunk.id;
            
            // Native Candle execution loop occurs here
            let embedding = self.generator.generate(chunk_id, &chunk.text)?;
            
            self.vector_store.add_vector(chunk_id, &embedding.vector)?;
            self.memory_store.insert(chunk, embedding.vector.into_vec());
        }

        if self.has_persisted_data() {
            self.save_to_disk()?;
        }

        Ok(IngestionResponse {
            document_id,
            chunks_count,
            success: true,
            execution_time_ms: start_time.elapsed().as_millis() as u64,
        })
    }

    pub fn query(
        &self,
        query_text: &str,
        top_k: Option<usize>,
        score_threshold: Option<f32>,
        filters: Option<BTreeMap<Box<str>, Box<str>>>, 
    ) -> Result<QueryResponse, RagError> {
        let limit = top_k.unwrap_or(self.default_top_k);
        let query_embedding = self.generator.generate(0, query_text)?;

        let retriever_filters: Option<HashMap<String, String>> = filters.as_ref().map(|f| {
            f.iter()
                .map(|(k, v)| (k.to_string(), v.to_string()))
                .collect()
        });

        let matched_results = self.retriever.retrieve(
            query_text,
            &query_embedding.vector,
            &self.vector_store,
            &self.memory_store,
            limit,
            retriever_filters,
        )?;

        let filtered_matches: Vec<_> = if let Some(threshold) = score_threshold {
            matched_results.into_iter().filter(|(_, score)| *score >= threshold).collect()
        } else {
            matched_results
        };

        let dto_matches: Vec<TextChunkMatch> = filtered_matches
            .iter()
            .map(|(chunk, score)| TextChunkMatch {
                chunk_id: chunk.id,
                document_id: chunk.document_id.clone().into(),
                text: chunk.text.clone().into(),
                score: *score,
                metadata: chunk.metadata.iter()
                    .map(|(k, v)| (k.clone().into(), v.clone().into()))
                    .collect(),
            })
            .collect();

        let context_block = ContextFormatter::format_context(&filtered_matches).into();

        Ok(QueryResponse {
            context_block,
            matches: dto_matches,
        })
    }

    pub fn save_to_disk(&self) -> Result<(), RagError> {
        let idx_p = self.index_path.as_ref().ok_or_else(|| RagError::SerializationError("Missing Index Dump Path Setup".into()))?;
        let chk_p = self.chunks_path.as_ref().ok_or_else(|| RagError::SerializationError("Missing Chunks Dump Path Setup".into()))?;

        let mut all_chunks = Vec::new();
        let mut counter = 1u64;
        
        while let Some(chunk) = self.memory_store.get_chunk(counter) {
            all_chunks.push(chunk.clone());
            counter += 1;
        }

        StorePersistence::save_chunks_to_disk(chk_p, &all_chunks)?;
        self.vector_store.save_index(idx_p)?;
        Ok(())
    }

    pub fn load_from_disk(&mut self) -> Result<(), RagError> {
        let idx_p = self.index_path.as_ref().ok_or_else(|| RagError::SerializationError("Missing Index Restoration Target Path".into()))?;
        let chk_p = self.chunks_path.as_ref().ok_or_else(|| RagError::SerializationError("Missing Chunks Restoration Target Path".into()))?;

        if !Path::new(idx_p).exists() || !Path::new(chk_p).exists() {
            return Err(RagError::IoError(std::io::Error::new(
                std::io::ErrorKind::NotFound,
                "Persistent index or chunk records do not exist yet.",
            )));
        }

        self.memory_store.clear();

        let historical_chunks = StorePersistence::load_chunks_from_disk(chk_p)?;
        self.vector_store.load_index(idx_p)?;

        for chunk in historical_chunks {
            self.memory_store.insert(chunk, vec![]);
        }

        Ok(())
    }
}

// ==============================================================================
// CLEAN NATIVE SERVICE TESTS
// ==============================================================================
#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::rag::embedding::NativeEmbedder;
    use std::sync::Arc;

    const BGE_VAULT_PATH: &str = "../llm_models/embedding/bge_safetensors_output";

    fn setup_test_service(index_path: Option<String>, chunks_path: Option<String>) -> Option<RagService> {
        if !Path::new(BGE_VAULT_PATH).exists() {
            return None; // Guard for headless testing envs without large assets
        }

        let pipeline = IngestionPipeline::new(512, 50); 
        let vector_store = UsearchStore::new(384, 1000).expect("Failed to init Usearch"); 
        let memory_store = MemoryVectorStore::new();
        
        let embedder = NativeEmbedder::load_from_vault(BGE_VAULT_PATH).unwrap();
        let generator = EmbeddingGenerator::new(Arc::new(embedder));
        let retriever = VectorRetriever::new(0.5);
        
        Some(RagService::new(
            pipeline,
            vector_store,
            memory_store,
            generator,
            retriever,
            5, 
            index_path,
            chunks_path,
        ))
    }

    #[test]
    fn test_has_persisted_data_validation() {
        let Some(service_no_path) = setup_test_service(None, None) else { return; };
        assert!(!service_no_path.has_persisted_data());

        let service_with_paths = setup_test_service(Some("index.bin".into()), Some("chunks.json".into())).unwrap();
        let _ = service_with_paths.has_persisted_data();
    }

    #[test]
    fn test_save_to_disk_missing_paths_errors() {
        let Some(service) = setup_test_service(None, None) else { return; };
        let result = service.save_to_disk();
        assert!(result.is_err());
    }
}