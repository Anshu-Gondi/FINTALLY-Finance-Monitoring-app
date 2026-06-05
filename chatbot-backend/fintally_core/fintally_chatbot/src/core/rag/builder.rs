use crate::core::rag::errors::RagError;
use crate::core::rag::service::RagService;
use crate::core::rag::ingestion::IngestionPipeline;
use crate::core::rag::vectorstore::{UsearchStore, MemoryVectorStore};
use crate::core::rag::embedding::EmbeddingGenerator;
use crate::core::rag::retrieval::VectorRetriever;

pub struct RagServiceBuilder {
    chunk_size: usize,
    chunk_overlap: usize,
    dimensions: usize,
    alpha: f32,
    default_top_k: usize,
    index_path: Option<String>,
    chunks_path: Option<String>,
}

impl RagServiceBuilder {
    /// Initializes a default builder tuned for low-overhead ONNX execution
    pub fn new() -> Self {
        Self {
            chunk_size: 200,          // Word boundary window limit
            chunk_overlap: 40,        // Overlap buffer count
            dimensions: 384,          // Optimized dimension size for lightweight models (e.g., all-MiniLM-L6-v2)
            alpha: 0.7,               // 70% Dense vector search weight, 30% Sparse token intersection weight
            default_top_k: 5,         // Matches returned per request by default
            index_path: None,
            chunks_path: None,
        }
    }

    pub fn with_chunk_size(mut self, size: usize) -> Self {
        self.chunk_size = size;
        self
    }

    pub fn with_chunk_overlap(mut self, overlap: usize) -> Self {
        self.chunk_overlap = overlap;
        self
    }

    pub fn with_dimensions(mut self, dims: usize) -> Self {
        self.dimensions = dims;
        self
    }

    pub fn with_alpha(mut self, alpha: f32) -> Self {
        self.alpha = alpha.clamp(0.0, 1.0);
        self
    }

    pub fn with_default_top_k(mut self, top_k: usize) -> Self {
        self.default_top_k = top_k;
        self
    }

    /// Assigns the persistent file system targets for hot-reloads
    pub fn with_persistence(mut self, index_path: &str, chunks_path: &str) -> Self {
        self.index_path = Some(index_path.to_string());
        self.chunks_path = Some(chunks_path.to_string());
        self
    }

    /// Allocates internal memory spaces and builds the operational service pipeline
    pub fn build(self) -> Result<RagService, RagError> {
        let pipeline = IngestionPipeline::new(self.chunk_size, self.chunk_overlap);
        let vector_store = UsearchStore::new(self.dimensions)?;
        let memory_store = MemoryVectorStore::new();
        let generator = EmbeddingGenerator::new();
        let retriever = VectorRetriever::new(self.alpha);

        let mut service = RagService::new(
            pipeline,
            vector_store,
            memory_store,
            generator,
            retriever,
            self.default_top_k,
            self.index_path,
            self.chunks_path,
        );

        // Auto-load historical snapshots from disk if paths are provided
        if service.has_persisted_data() {
            // Fails silently on the initial boot if backup binaries don't exist yet
            let _ = service.load_from_disk();
        }

        Ok(service)
    }
}