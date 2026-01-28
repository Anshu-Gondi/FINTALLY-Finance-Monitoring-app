use pyo3::prelude::*;

pub mod llm;
pub mod finance;

pub fn register(m: &Bound<'_, PyModule>) -> PyResult<()> {
    let py = m.py();

    // ---- llm submodule ----
    let llm_mod = PyModule::new_bound(py, "llm")?;
    llm::register(&llm_mod)?;
    m.add_submodule(&llm_mod)?;

    // ---- finance submodule ----
    let finance_mod = PyModule::new_bound(py, "finance")?;
    finance::register(&finance_mod)?;
    m.add_submodule(&finance_mod)?;

    Ok(())
}
