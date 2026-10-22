pub mod builder;
pub mod chunking;
pub mod context;
pub mod dto;
pub mod embedding;
pub mod errors;
pub mod ingestion;
pub mod retrieval;
pub mod service;
pub mod vectorstore;

// Clean re-exports for the PyO3 binding layer
pub use builder::RagServiceBuilder;
pub use errors::RagError;
pub use service::RagService;
