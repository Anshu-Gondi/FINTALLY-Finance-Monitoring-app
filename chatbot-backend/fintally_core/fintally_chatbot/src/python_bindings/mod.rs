use pyo3::prelude::*;

pub mod llm;
pub mod finance;
pub mod rag;

pub fn register(py: Python, m: &PyModule) -> PyResult<()> {
    let py = m.py();

    // ---- 1. llm submodule ----
    let llm_mod = PyModule::new(py, "llm")?;
    llm::register(py, &llm_mod)?;
    m.add_submodule(&llm_mod)?;

    // ---- 2. finance submodule ----
    let finance_mod = PyModule::new(py, "finance")?;
    finance::register(py, &finance_mod)?;
    m.add_submodule(&finance_mod)?;

    // ---- 3. rag submodule ----
    let rag_mod = PyModule::new(py, "rag")?;
    rag::register(py, &rag_mod)?;
    m.add_submodule(&rag_mod)?;

    // ---- 4. Register submodules into sys.modules cache ----
    // This allows standard Python "from fintally_chatbot.rag import X" syntax!
    let sys = py.import("sys")?;
    let sys_modules = sys.getattr("modules")?;
    
    sys_modules.set_item("fintally_chatbot.llm", llm_mod)?;
    sys_modules.set_item("fintally_chatbot.finance", finance_mod)?;
    sys_modules.set_item("fintally_chatbot.rag", rag_mod)?;

    Ok(())
}