pub mod loader;
pub mod documents;
pub mod pipeline;

// Clean public interface for running the document pipeline
pub use pipeline::IngestionPipeline;
