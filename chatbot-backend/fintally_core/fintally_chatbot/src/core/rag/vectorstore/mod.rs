// 1. Register the sub-modules matching your exact folder structure
pub mod similarity;
pub mod usearch_store;
pub mod persistence;
pub mod memory_store;

// 2. Clean Public Interface Re-exports
// This allows your `service.rs` and `retriever.rs` to consume your vector stores cleanly
// using `vectorstore::MemoryVectorStore` instead of making them type out the full, repetitive paths.
pub use memory_store::MemoryVectorStore;
pub use usearch_store::UsearchStore;