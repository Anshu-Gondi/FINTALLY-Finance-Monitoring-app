use pyo3::prelude::*;

pub mod core;
pub mod python_bindings;

#[pymodule]
fn _native(m: &Bound<'_, PyModule>) -> PyResult<()> {
    python_bindings::register(m)?;
    Ok(())
}
