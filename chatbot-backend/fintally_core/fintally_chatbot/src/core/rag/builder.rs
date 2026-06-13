use std::path::Path;
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
    initial_capacity: usize, // Expose capacity parameter for the underlying HNSW graph
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
            initial_capacity: 5000,   // Default reservation matching our optimized USearch layer
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

    /// Explicitly configures the pre-allocated index bounds to avoid mid-ingestion resizing overhead
    pub fn with_initial_capacity(mut self, capacity: usize) -> Self {
        self.initial_capacity = capacity;
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
        
        // COMPILATION FIX: Propagate the initial capacity parameter directly into the HNSW initialization layer
        let vector_store = UsearchStore::new(self.dimensions, self.initial_capacity)?;
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

        // Explicit Persistence Validation: Only attempt to load if files are present.
        // This avoids masking actual file corruption errors behind a blind catch-all.
        if let (Some(idx), Some(chk)) = (&service.index_path, &service.chunks_path) {
            if Path::new(idx).exists() && Path::new(chk).exists() {
                service.load_from_disk()?;
            }
        }

        Ok(service)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_builder_initial_defaults() {
        let builder = RagServiceBuilder::new();
        
        // Assert that out-of-the-box settings align with structural choices
        assert_eq!(builder.chunk_size, 200);
        assert_eq!(builder.chunk_overlap, 40);
        assert_eq!(builder.dimensions, 384);
        assert_eq!(builder.initial_capacity, 5000);
        assert!((builder.alpha - 0.7).abs() < f32::EPSILON);
        assert_eq!(builder.default_top_k, 5);
        assert!(builder.index_path.is_none());
        assert!(builder.chunks_path.is_none());
    }

    #[test]
    fn test_builder_fluent_setters() {
        // Assert that parameter adjustments chain correctly
        let builder = RagServiceBuilder::new()
            .with_chunk_size(500)
            .with_chunk_overlap(100)
            .with_dimensions(768)
            .with_initial_capacity(25000)
            .with_default_top_k(20)
            .with_persistence("active_index.usearch", "active_chunks.json");

        assert_eq!(builder.chunk_size, 500);
        assert_eq!(builder.chunk_overlap, 100);
        assert_eq!(builder.dimensions, 768);
        assert_eq!(builder.initial_capacity, 25000);
        assert_eq!(builder.default_top_k, 20);
        assert_eq!(builder.index_path.as_deref(), Some("active_index.usearch"));
        assert_eq!(builder.chunks_path.as_deref(), Some("active_chunks.json"));
    }

    #[test]
    fn test_alpha_clamping_boundaries() {
        // Test lower-bound clamp behavior
        let negative_alpha_builder = RagServiceBuilder::new().with_alpha(-0.5);
        assert_eq!(negative_alpha_builder.alpha, 0.0);

        // Test upper-bound clamp behavior
        let overflow_alpha_builder = RagServiceBuilder::new().with_alpha(2.75);
        assert_eq!(overflow_alpha_builder.alpha, 1.0);

        // Test normal operational range passing unhindered
        let valid_alpha_builder = RagServiceBuilder::new().with_alpha(0.35);
        assert!((valid_alpha_builder.alpha - 0.35).abs() < f32::EPSILON);
    }

    #[test]
    fn test_successful_clean_build_orchestration() {
        let builder = RagServiceBuilder::new()
            .with_chunk_size(150)
            .with_dimensions(128)
            .with_initial_capacity(1000);

        let setup_result = builder.build();
        
        // The build method must successfully assemble subcomponents when persistence isn't present
        assert!(setup_result.is_ok(), "Builder failed inside component assembly generation: {:?}", setup_result.err());
        
        let service = setup_result.unwrap();
        assert!(!service.has_persisted_data());
    }
}