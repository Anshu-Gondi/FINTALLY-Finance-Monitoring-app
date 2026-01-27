use super::engine::LlmEngine;
use crate::core::utils::errors::AppError;
use pyo3::prelude::*;

pub struct PythonEngine;

impl LlmEngine for PythonEngine {
    fn generate(&self, prompt: &str, max_tokens: usize) -> Result<String, AppError> {
        Python::with_gil(|py| -> PyResult<String> {
            let module = py.import("services.llm_bridge")?;
            let func = module.getattr("llama_generate")?;
            let result = func.call1((prompt, max_tokens))?;
            result.extract()
        })
        .map_err(|e| AppError::InternalError(e.to_string()))
    }

    fn embed(&self, text: &str) -> Result<Vec<f32>, AppError> {
        Python::with_gil(|py| -> PyResult<Vec<f32>> {
            let module = py.import("services.llm_bridge")?;
            let func = module.getattr("llama_embed")?;
            let result = func.call1((text,))?;
            result.extract()
        })
        .map_err(|e| AppError::InternalError(e.to_string()))
    }
}
