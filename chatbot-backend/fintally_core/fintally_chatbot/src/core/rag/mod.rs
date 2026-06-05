pub mod errors;
pub mod dto;
pub mod chunking;
pub mod ingestion;
pub mod vectorstore;
pub mod embedding;
pub mod retrieval;
pub mod context;
pub mod builder;
pub mod service;

// Clean re-exports for the PyO3 binding layer
pub use errors::RagError;
pub use builder::RagServiceBuilder;
pub use service::RagService;