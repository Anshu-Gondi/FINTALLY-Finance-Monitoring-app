use pyo3::prelude::*;

pub mod engine;

/// Registers the RAG submodule components
pub fn register(_py: Python, m: &PyModule) -> PyResult<()> {
    m.add_class::<engine::PyRagEngine>()?;
    Ok(())
}