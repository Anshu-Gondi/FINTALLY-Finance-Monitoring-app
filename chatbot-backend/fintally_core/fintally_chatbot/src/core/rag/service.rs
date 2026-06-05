use std::collections::HashMap;
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
    index_path: Option<String>,
    chunks_path: Option<String>,
}

impl RagService {
    /// Instantiates the core service orchestration layer
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

    /// Checks if filesystem persistence paths are fully configured
    pub fn has_persisted_data(&self) -> bool {
        self.index_path.is_some() && self.chunks_path.is_some()
    }

    /// Ingests a raw file, creates token overlapping chunks, 
    /// executes light ONNX vector generation, and registers items inside HNSW graphs.
    pub fn ingest_file(&mut self, file_path: &str) -> Result<IngestionResponse, RagError> {
        let start_time = Instant::now();
        
        // 1. Process files through text-splitting pipeline layouts
        let chunks = self.pipeline.process_file(file_path)?;
        let chunks_count = chunks.len();
        
        if chunks.is_empty() {
            return Ok(IngestionResponse {
                document_id: file_path.to_string(),
                chunks_count: 0,
                success: false,
                execution_time_ms: start_time.elapsed().as_millis() as u64,
            });
        }

        let document_id = chunks[0].document_id.clone();

        // 2. Compute vectors via ONNX client loop and load indices
        for chunk in chunks {
            let chunk_id = chunk.id;
            
            // Execute embedding generation (invoking python runtime)
            let embedding = self.generator.generate(chunk_id, &chunk.text)?;
            
            // Register inside both standard USearch graph & local metadata map cache
            self.vector_store.add_vector(chunk_id, &embedding.vector)?;
            self.memory_store.insert(chunk, embedding.vector);
        }

        // 3. Automatically dump cold-storage backups to disk if path parameters exist
        if self.has_persisted_data() {
            let _ = self.save_to_disk();
        }

        Ok(IngestionResponse {
            document_id,
            chunks_count,
            success: true,
            execution_time_ms: start_time.elapsed().as_millis() as u64,
        })
    }

    /// Queries the hybrid index layer, scores results, and crafts an injection block context
    pub fn query(
        &self,
        query_text: &str,
        top_k: Option<usize>,
        score_threshold: Option<f32>,
        filters: Option<HashMap<String, String>>,
    ) -> Result<QueryResponse, RagError> {
        let limit = top_k.unwrap_or(self.default_top_k);

        // 1. Compute embedding vector layout for the raw input query via ONNX
        let query_embedding = self.generator.generate(0, query_text)?;

        // 2. Retrieve matched references executing unified graph and token hybrid combinations
        let matched_results = self.retriever.retrieve(
            query_text,
            &query_embedding.vector,
            &self.vector_store,
            &self.memory_store,
            limit,
            filters,
        )?;

        // 3. Filter entries matching minimum target performance scores if passed
        let filtered_matches: Vec<(crate::core::rag::embedding::types::DocumentChunk, f32)> = if let Some(threshold) = score_threshold {
            matched_results.into_iter().filter(|(_, score)| *score >= threshold).collect()
        } else {
            matched_results
        };

        // 4. Transform native memory chunks out into structural cross-layer DTO layouts
        let mut dto_matches = Vec::with_capacity(filtered_matches.len());
        for (chunk, score) in &filtered_matches {
            dto_matches.push(TextChunkMatch {
                chunk_id: chunk.id,
                document_id: chunk.document_id.clone(),
                text: chunk.text.clone(),
                score: *score,
                metadata: chunk.metadata.clone(),
            });
        }

        // 5. Compile single combined context data blocks optimized for immediate LLM injection
        let context_block = ContextFormatter::format_context(&filtered_matches);

        Ok(QueryResponse {
            context_block,
            matches: dto_matches,
        })
    }

    /// Flushes all hot metadata clusters and vector maps down onto local storage
    pub fn save_to_disk(&self) -> Result<(), RagError> {
        let idx_p = self.index_path.as_ref().ok_or_else(|| RagError::SerializationError("Missing Index Dump Path Setup".into()))?;
        let chk_p = self.chunks_path.as_ref().ok_or_else(|| RagError::SerializationError("Missing Chunks Dump Path Setup".into()))?;

        // Extract and serialize text-chunk arrays out of the memory cache store maps
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

    /// Restores vector indices and string collections from static snapshot footprints
    pub fn load_from_disk(&mut self) -> Result<(), RagError> {
        let idx_p = self.index_path.as_ref().ok_or_else(|| RagError::SerializationError("Missing Index Restoration Target Path".into()))?;
        let chk_p = self.chunks_path.as_ref().ok_or_else(|| RagError::SerializationError("Missing Chunks Restoration Target Path".into()))?;

        if !Path::new(idx_p).exists() || !Path::new(chk_p).exists() {
            return Err(RagError::IoError(std::io::Error::new(
                std::io::ErrorKind::NotFound,
                "Persistent index or chunk records do not exist yet for this workspace instance.",
            )));
        }

        // 1. Wipe current runtime items inside our registries to cleanly prevent dirty overlapping indices
        self.memory_store.clear();

        // 2. Fetch records and read serializations straight back into structural layouts
        let historical_chunks = StorePersistence::load_chunks_from_disk(chk_p)?;
        self.vector_store.load_index(idx_p)?;

        for chunk in historical_chunks {
            // Note: Since raw index graphs reload directly into C-space inside USearch,
            // we populate only our local lookup metadata store mappings to stitch things back together.
            self.memory_store.insert(chunk, vec![]);
        }

        Ok(())
    }
}