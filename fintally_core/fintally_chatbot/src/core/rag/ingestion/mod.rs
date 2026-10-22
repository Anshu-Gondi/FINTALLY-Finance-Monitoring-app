pub mod documents;
pub mod loader;
pub mod pipeline;

// Clean public interface for running the document pipeline
pub use pipeline::IngestionPipeline;
