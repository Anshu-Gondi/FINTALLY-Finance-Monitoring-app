pub mod assistant;

// Re-export the tool executor for seamless service workspace utilization
pub use assistant::execute_tool;