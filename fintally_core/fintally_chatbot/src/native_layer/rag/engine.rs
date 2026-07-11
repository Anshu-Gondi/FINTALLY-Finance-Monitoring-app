use std::collections::{BTreeMap, HashMap};
use std::path::Path;
use std::sync::Arc;

use crate::core::rag::builder::RagServiceBuilder;
use crate::core::rag::service::RagService;
use crate::core::rag::errors::RagError;
use crate::core::rag::dto::{IngestionResponse, QueryResponse};
use crate::core::rag::embedding::NativeEmbedder;

/// High-Performance Local RAG Execution Engine
pub struct RagEngine {
    service: RagService,
}

impl RagEngine {
    /// Instantiates a pure-Rust local search context window engine.
    /// Expects a path to the unquantized BGE Safetensors asset folder.
    pub fn new<P: AsRef<Path>>(
        model_vault_path: P,
        chunk_size: usize,
        chunk_overlap: usize,
        dimensions: usize,
        alpha: f32,
        default_top_k: usize,
        index_path: Option<String>,
        chunks_path: Option<String>,
    ) -> Result<Self, RagError> {
        // 1. Initialize the native Candle embedder
        let embedder = NativeEmbedder::load_from_vault(model_vault_path)?;
        let embedder_ref = Arc::new(embedder);

        // 2. Build the orchestration service
        let mut builder = RagServiceBuilder::new()
            .with_chunk_size(chunk_size)
            .with_chunk_overlap(chunk_overlap)
            .with_dimensions(dimensions)
            .with_alpha(alpha)
            .with_default_top_k(default_top_k);

        if let (Some(idx), Some(chk)) = (index_path, chunks_path) {
            builder = builder.with_persistence(&idx, &chk);
        }

        let service = builder.build(embedder_ref)?;
        Ok(Self { service })
    }

    /// Ingests a flat document file, applies chunk indexing, runs matrix inferences, and writes graphs
    #[inline]
    pub fn ingest_file(&mut self, file_path: &str) -> Result<IngestionResponse, RagError> {
        self.service.ingest_file(file_path)
    }

    /// Queries the hybrid index layer using clean Rust collection payloads
    pub fn query(
        &self,
        query_text: &str,
        top_k: Option<usize>,
        score_threshold: Option<f32>,
        filters: Option<HashMap<String, String>>,
    ) -> Result<QueryResponse, RagError> {
        let optimized_filters: Option<BTreeMap<Box<str>, Box<str>>> = filters.map(|f| {
            f.into_iter()
                .map(|(k, v)| (k.into_boxed_str(), v.into_boxed_str()))
                .collect()
        });

        self.service.query(query_text, top_k, score_threshold, optimized_filters)
    }

    /// Explicitly commands the HNSW layer to persist snapshot data to disk
    #[inline]
    pub fn save_to_disk(&self) -> Result<(), RagError> {
        self.service.save_to_disk()
    }

    /// Manually loads vector configurations from pre-existing index footprints
    #[inline]
    pub fn load_from_disk(&mut self) -> Result<(), RagError> {
        self.service.load_from_disk()
    }
}