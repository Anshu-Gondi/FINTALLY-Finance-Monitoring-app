use pyo3::prelude::*;
use serde_json::Value;

use crate::core::llm::planner::Planner;
use crate::core::utils::errors::AppError;

/// Python → Rust tool executor
#[pyfunction]
pub fn execute_tool(
    tool_name: String,
    args_json: String, // <-- JSON STRING, not PyAny
) -> PyResult<String> {
    // Parse JSON string → serde_json::Value
    let args: Value = serde_json::from_str(&args_json)
        .map_err(|e| pyo3::exceptions::PyValueError::new_err(e.to_string()))?;

    let rt = get_runtime();

    let result: Value = rt
        .block_on(async {
            Planner::execute(&tool_name, args).await
        })
        .map_err(|e: AppError| {
            pyo3::exceptions::PyRuntimeError::new_err(e.to_string())
        })?;

    // Return JSON string back to Python
    serde_json::to_string(&result)
        .map_err(|e| pyo3::exceptions::PyRuntimeError::new_err(e.to_string()))
}

/// Global Tokio runtime
fn get_runtime() -> &'static tokio::runtime::Runtime {
    use std::sync::OnceLock;

    static RUNTIME: OnceLock<tokio::runtime::Runtime> = OnceLock::new();

    RUNTIME.get_or_init(|| {
        tokio::runtime::Builder::new_multi_thread()
            .enable_all()
            .thread_name("py-planner")
            .build()
            .expect("Failed to build Tokio runtime")
    })
}

