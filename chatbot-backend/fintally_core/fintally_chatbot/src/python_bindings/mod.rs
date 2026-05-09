use pyo3::prelude::*;

pub mod llm;
pub mod finance;

pub fn register(py: Python, m: &PyModule) -> PyResult<()> {
    let py = m.py();

    // ---- llm submodule ----
    let llm_mod = PyModule::new(py, "llm")?;
    llm::register(py, &llm_mod)?;
    m.add_submodule(&llm_mod)?;

    // ---- finance submodule ----
    let finance_mod = PyModule::new(py, "finance")?;
    finance::register(py, &finance_mod)?;
    m.add_submodule(&finance_mod)?;

    Ok(())
}
