use pyo3::prelude::*;

pub mod core;
pub mod python_bindings;

#[pymodule]
fn fintally_chatbot(py: Python, m: &PyModule) -> PyResult<()> {
    python_bindings::register(py, m)?;
    Ok(())
}
