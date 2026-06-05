use usearch::{Index, IndexOptions, MetricKind, ScalarKind};
use crate::core::rag::errors::RagError;

pub struct UsearchStore {
    index: Index,
    dimensions: usize,
}

impl UsearchStore {
    /// Allocates a new native HNSW vector cluster space
    pub fn new(dimensions: usize) -> Result<Self, RagError> {
        let mut options = IndexOptions::default();
        options.dimensions = dimensions;
        options.metric = MetricKind::Cos;            // Fast directional cosine distance metrics
        options.quantization = ScalarKind::I8;       // Native 8-bit vector array scaling optimization
        
        let index = Index::new(&options)
            .map_err(|_| RagError::VectorStoreError("Failed to initialize native USearch graph index context.".into()))?;

        // Pre-reserve standard capacity spaces to limit allocation loops
        index.reserve(5000)
            .map_err(|_| RagError::VectorStoreError("Failed to reserve storage buffer steps within native index allocator.".into()))?;

        Ok(Self { index, dimensions })
    }

    /// Indexes an f32 vector into the quantized USearch matrix under a given chunk ID
    pub fn add_vector(&self, chunk_id: u64, vector: &[f32]) -> Result<(), RagError> {
        if vector.len() != self.dimensions {
            return Err(RagError::VectorStoreError(
                format!("Vector dimension layout size mismatch: expected {}, got {}.", self.dimensions, vector.len())
            ));
        }

        self.index.add(chunk_id, vector)
            .map_err(|_| RagError::VectorStoreError(format!("Failed to register vector item ID: {}", chunk_id)))?;

        Ok(())
    }

    /// Finds matching vectors inside the HNSW index layer
    pub fn search_vectors(&self, query_vector: &[f32], top_k: usize) -> Result<Vec<(u64, f32)>, RagError> {
        if query_vector.len() != self.dimensions {
            return Err(RagError::VectorStoreError("Query vector dimension target configuration out of sync.".into()));
        }

        let matches = self.index.search(query_vector, top_k)
            .map_err(|_| RagError::VectorStoreError("Native graph query search routine step execution failure.".into()))?;

        let mut results = Vec::with_capacity(matches.keys.len());
        for i in 0..matches.keys.len() {
            // USearch returns raw distance. Convert to a similarity score where higher is better
            let similarity_score = 1.0 - matches.distances[i];
            results.push((matches.keys[i], similarity_score));
        }

        Ok(results)
    }

    /// Flushes graph layouts straight to static disk files safely
    pub fn save_index(&self, path: &str) -> Result<(), RagError> {
        self.index.save(path)
            .map_err(|_| RagError::VectorStoreError("Failed to dump native graph data array sets onto disk path file target.".into()))?;
        Ok(())
    }

    /// Reloads compiled index mappings from local file records
    pub fn load_index(&self, path: &str) -> Result<(), RagError> {
        self.index.load(path)
            .map_err(|_| RagError::VectorStoreError("Failed to fetch binary data arrays from local filesystem targets.".into()))?;
        Ok(())
    }
}