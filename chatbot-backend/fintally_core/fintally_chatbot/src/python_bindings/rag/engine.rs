use pyo3::prelude::*;
use pyo3::types::{PyDict, PyList};
use pyo3::exceptions::PyRuntimeError;
use std::collections::{HashMap, BTreeMap};

use crate::core::rag::builder::RagServiceBuilder;
use crate::core::rag::service::RagService;
use crate::core::rag::errors::RagError;

// Helper to convert native Rust RAG exceptions directly into standard Python exceptions
fn to_py_err(err: RagError) -> PyErr {
    PyRuntimeError::new_err(err.to_string())
}

#[pyclass(name = "RagEngine")]
pub struct PyRagEngine {
    service: RagService,
}

#[pymethods]
impl PyRagEngine {
    #[new]
    #[pyo3(signature = (chunk_size=200, chunk_overlap=40, dimensions=384, alpha=0.7, default_top_k=5, index_path=None, chunks_path=None))]
    fn new(
        chunk_size: usize,
        chunk_overlap: usize,
        dimensions: usize,
        alpha: f32,
        default_top_k: usize,
        index_path: Option<String>,
        chunks_path: Option<String>,
    ) -> PyResult<Self> {
        let mut builder = RagServiceBuilder::new()
            .with_chunk_size(chunk_size)
            .with_chunk_overlap(chunk_overlap)
            .with_dimensions(dimensions)
            .with_alpha(alpha)
            .with_default_top_k(default_top_k);

        if let (Some(idx), Some(chk)) = (index_path, chunks_path) {
            builder = builder.with_persistence(&idx, &chk);
        }

        let service = builder.build().map_err(to_py_err)?;
        Ok(PyRagEngine { service })
    }

    /// Ingests a finance file, cuts text windows, runs ONNX vectors, and builds indices
    fn ingest_file(&mut self, file_path: &str) -> PyResult<PyObject> {
        let response = self.service.ingest_file(file_path).map_err(to_py_err)?;
        
        Python::with_gil(|py| {
            let dict = PyDict::new(py);
            // .as_ref() drops Box<str> down to a standard &str slice which PyO3 can serialize
            dict.set_item("document_id", response.document_id.as_ref())?;
            dict.set_item("chunks_count", response.chunks_count)?;
            dict.set_item("success", response.success)?;
            dict.set_item("execution_time_ms", response.execution_time_ms)?;
            Ok(dict.to_object(py))
        })
    }

    /// Queries the hybrid HNSW index and returns a prompt-ready string alongside metadata
    #[pyo3(signature = (query_text, top_k=None, score_threshold=None, filters=None))]
    fn query(
        &self,
        query_text: &str,
        top_k: Option<usize>,
        score_threshold: Option<f32>,
        filters: Option<HashMap<String, String>>,
    ) -> PyResult<PyObject> {
        // Convert incoming Python HashMap filters into the optimized BTreeMap expected by the service
        let optimized_filters: Option<BTreeMap<Box<str>, Box<str>>> = filters.map(|f| {
            f.into_iter()
             .map(|(k, v)| (k.into_boxed_str(), v.into_boxed_str()))
             .collect()
        });

        let response = self.service.query(query_text, top_k, score_threshold, optimized_filters).map_err(to_py_err)?;
        
        Python::with_gil(|py| {
            let result_dict = PyDict::new(py);
            result_dict.set_item("context_block", response.context_block.as_ref())?;
            
            let matches_list = PyList::empty(py);
            for chunk_match in response.matches {
                let match_dict = PyDict::new(py);
                match_dict.set_item("chunk_id", chunk_match.chunk_id)?;
                match_dict.set_item("document_id", chunk_match.document_id.as_ref())?;
                match_dict.set_item("text", chunk_match.text.as_ref())?;
                match_dict.set_item("score", chunk_match.score)?;
                
                // Manually parse the BTreeMap<Box<str>, Box<str>> metadata into a valid PyDict
                let metadata_dict = PyDict::new(py);
                for (k, v) in &chunk_match.metadata {
                    metadata_dict.set_item(k.as_ref(), v.as_ref())?;
                }
                match_dict.set_item("metadata", metadata_dict)?;
                
                matches_list.append(match_dict)?;
            }
            
            result_dict.set_item("matches", matches_list)?;
            Ok(result_dict.to_object(py))
        })
    }

    /// Manually dumps the vector layout binaries to physical storage paths
    fn save_to_disk(&self) -> PyResult<()> {
        self.service.save_to_disk().map_err(to_py_err)
    }

    /// Restores vector indices and string structures from physical storage paths
    fn load_from_disk(&mut self) -> PyResult<()> {
        self.service.load_from_disk().map_err(to_py_err)
    }
}