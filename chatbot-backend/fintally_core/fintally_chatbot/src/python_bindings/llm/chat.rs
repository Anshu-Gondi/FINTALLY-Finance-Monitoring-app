use pyo3::prelude::*;
use pyo3::pyasync::IterANextOutput;
use pyo3::IntoPy;
use std::sync::Arc;

// Explicitly bring StreamExt into scope for .next() on async items
use futures_util::StreamExt;
use tokio::sync::mpsc::{channel, Receiver};
use tokio::sync::Mutex;
use tokio_util::sync::CancellationToken;

use crate::core::llm::model::LLM;
use crate::core::utils::errors::AppError;

/// ─────────────────────────────────────────────
/// Python-visible LLM
/// ─────────────────────────────────────────────
#[pyclass]
pub struct PyLLM {
    pub(crate) inner: Arc<LLM>,
}

#[pymethods]
impl PyLLM {
    #[new]
    fn new() -> PyResult<Self> {
        Err(pyo3::exceptions::PyNotImplementedError::new_err("Use create_llm() factory function"))
    }

    /// ✅ Async generate
    #[pyo3(signature = (prompt, context = None))]
    fn generate<'py>(
        &self,
        py: Python<'py>,
        prompt: String,
        context: Option<String>,
    ) -> PyResult<&'py PyAny> {
        let inner = self.inner.clone();

        pyo3_asyncio::tokio::future_into_py(py, async move {
            let full_prompt = crate::core::llm::prompt::Prompt::build(&prompt, context.as_deref())
                .map_err(app_error_to_py)?;

            let result = inner
                .generate_text(&full_prompt, context.as_deref())
                .await
                .map_err(app_error_to_py)?;

            Ok(result)
        })
    }

    /// ✅ Async embeddings
    #[pyo3(signature = (text))]
    fn embed<'py>(&self, py: Python<'py>, text: String) -> PyResult<&'py PyAny> {
        let inner = self.inner.clone();

        pyo3_asyncio::tokio::future_into_py(py, async move {
            let result = inner.embed_text(&text).await.map_err(app_error_to_py)?;

            Ok(result)
        })
    }

    /// 🔥 Streaming (correct async design)
    #[pyo3(signature = (prompt, context = None))]
    fn stream<'py>(
        &self,
        py: Python<'py>,
        prompt: String,
        context: Option<String>,
    ) -> PyResult<&'py PyAny> {
        let inner = self.inner.clone();

        pyo3_asyncio::tokio::future_into_py(py, async move {
            let full_prompt = crate::core::llm::prompt::Prompt::build(&prompt, context.as_deref())
                .map_err(app_error_to_py)?;

            let cancelable = inner
                .stream_text(&full_prompt, context.as_deref())
                .await
                .map_err(app_error_to_py)?;

            let cancel_token = cancelable.cancel.clone();
            let cancel_for_task = cancel_token.clone();
            
            // Fix E0599 & E0282: Pin the stream explicitly to the stack layer 
            // so futures_util::StreamExt can resolve the underlying item types cleanly.
            let mut stream = Box::pin(cancelable.stream);

            let (tx, rx) = channel(32);

            tokio::spawn(async move {
                loop {
                    tokio::select! {
                        _ = cancel_for_task.cancelled() => {
                            break;
                        }

                        item = stream.next() => {
                            match item {
                                Some(Ok(token)) => {
                                    if tx.send(Ok(token)).await.is_err() {
                                        break;
                                    }
                                }
                                Some(Err(e)) => {
                                    let _ = tx.send(Err(app_error_to_py(e))).await;
                                    break;
                                }
                                None => break,
                            }
                        }
                    }
                }
            });

            Ok(PyStream {
                rx: Arc::new(Mutex::new(rx)),
                cancel: cancel_token,
            })
        })
    }
}

/// ─────────────────────────────────────────────
/// Async Python Stream Object
/// ─────────────────────────────────────────────
#[pyclass]
pub struct PyStream {
    rx: Arc<Mutex<Receiver<Result<String, PyErr>>>>,
    cancel: CancellationToken,
}

#[pymethods]
impl PyStream {
    /// async for support
    fn __aiter__(slf: PyRef<Self>) -> PyRef<Self> {
        slf
    }

    fn __anext__<'py>(
        slf: PyRef<'py, Self>,
        py: Python<'py>,
    ) -> PyResult<IterANextOutput<PyObject, PyObject>> {
        let rx = slf.rx.clone();

        let fut = pyo3_asyncio::tokio::future_into_py(py, async move {
            let mut rx = rx.lock().await;

            match rx.recv().await {
                Some(Ok(token)) => Ok(token),
                Some(Err(e)) => Err(e),
                None => Err(pyo3::exceptions::PyStopAsyncIteration::new_err("Stream ended")),
            }
        })?;

        Ok(IterANextOutput::Yield(fut.into_py(py)))
    }

    /// 🔴 Cancel generation
    fn cancel(&self) {
        self.cancel.cancel();
    }
}

/// ─────────────────────────────────────────────
/// Error mapping
/// ─────────────────────────────────────────────
fn app_error_to_py(err: AppError) -> PyErr {
    // Fix E0433: This handles mapping errors gracefully out to Python space.
    // If your app error layout uses specialized domain sub-variants, parse them cleanly here.
    pyo3::exceptions::PyRuntimeError::new_err(err.to_string())
}