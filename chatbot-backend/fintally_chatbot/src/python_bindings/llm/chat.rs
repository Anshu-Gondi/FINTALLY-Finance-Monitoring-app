use pyo3::prelude::*;
use std::pin::Pin;
use std::sync::Arc;

use futures_util::StreamExt;

use crate::core::llm::model::LLM;
use crate::core::utils::errors::AppError;

/// Python-visible LLM object
#[pyclass]
pub struct PyLLM {
    pub(crate) inner: Arc<LLM>,
}

#[pymethods]
impl PyLLM {
    /// 🔒 Constructor hidden — use factory
    #[new]
    fn new() -> PyResult<Self> {
        Err(pyo3::exceptions::PyNotImplementedError::new_err("Use create_llm() factory function"))
    }

    /// Non-streaming generation (BLOCKING)
    #[pyo3(signature = (prompt, context = None))]
    fn generate(&self, prompt: String, context: Option<String>) -> PyResult<String> {
        let full_prompt = crate::core::llm::prompt::Prompt
            ::build(&prompt, context.as_deref())
            .map_err(app_error_to_py)?;

        futures_executor
            ::block_on(self.inner.generate_text(&full_prompt, context.as_deref()))
            .map_err(app_error_to_py)
    }

    /// 🔥 STREAMING — Python iterator (blocking, safe)
    #[pyo3(signature = (prompt, context = None))]
    fn stream(&self, prompt: String, context: Option<String>) -> PyResult<StreamIterator> {
        let full_prompt = crate::core::llm::prompt::Prompt
            ::build(&prompt, context.as_deref())
            .map_err(app_error_to_py)?;

        let cancelable = futures_executor
            ::block_on(self.inner.stream_text(&full_prompt, context.as_deref()))
            .map_err(app_error_to_py)?;

        Ok(StreamIterator {
            stream: cancelable.stream,
            cancel: cancelable.cancel,
        })
    }

    /// Embeddings (blocking)
    #[pyo3(signature = (text))]
    fn embed(&self, text: String) -> PyResult<Vec<f32>> {
        futures_executor::block_on(self.inner.embed_text(&text)).map_err(app_error_to_py)
    }
}


/// ─────────────────────────────────────────────
/// Stream → Python iterator adapter
/// ─────────────────────────────────────────────
#[pyclass]
pub struct StreamIterator {
    stream: Pin<Box<dyn futures_core::Stream<Item = Result<String, AppError>> + Send>>,
    cancel: tokio_util::sync::CancellationToken,
}
#[pymethods]
impl StreamIterator {
    fn __iter__(slf: PyRef<Self>) -> PyRef<Self> {
        slf
    }

    fn __next__(mut slf: PyRefMut<Self>) -> PyResult<Option<String>> {
        match futures_executor::block_on(slf.stream.next()) {
            Some(Ok(token)) => Ok(Some(token)),
            Some(Err(e)) => Err(app_error_to_py(e)),
            None => Ok(None),
        }
    }

    /// 🔴 STOP BUTTON
    fn cancel(&self) {
        self.cancel.cancel();
    }
}

/// Convert Rust AppError → Python Exception
fn app_error_to_py(err: AppError) -> PyErr {
    pyo3::exceptions::PyRuntimeError::new_err(err.to_string())
}
