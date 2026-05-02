pub mod chat;

use pyo3::prelude::*;
use std::sync::Arc;

use crate::core::llm::model::LLM;
use crate::core::llm::python_engine::PythonLlamaEngine;
use crate::python_bindings::llm::chat::PyLLM;

#[pyfunction]
pub fn create_llm(model_name: String, max_tokens: usize) -> PyResult<PyLLM> {
    let engine = Box::new(PythonLlamaEngine);
    let llm = LLM::new(engine, &model_name, max_tokens);

    Ok(PyLLM {
        inner: Arc::new(llm),
    })
}

#[pyfunction]
fn stop_generation(py: Python) -> PyResult<()> {
    let module = py.import_bound("python_llama")?;
    module.getattr("stop")?.call0()?;
    Ok(())
}

/// Register `llm` submodule contents
pub fn register(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(create_llm, m)?)?;
    m.add_function(wrap_pyfunction!(stop_generation, m)?)?;
    m.add_class::<PyLLM>()?;
    Ok(())
}
