use pyo3::prelude::*;

pub mod assistant;

pub fn register(py: Python, m: &PyModule) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(assistant::execute_tool, m)?)?;
    Ok(())
}
