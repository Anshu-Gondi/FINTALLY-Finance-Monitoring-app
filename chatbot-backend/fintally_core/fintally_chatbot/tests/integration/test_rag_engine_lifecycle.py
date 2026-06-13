# chatbot-backend/tests/integration/test_rag_engine_lifecycle.py
"""
Fintally PyO3 Native RAG Engine Integration Test Suite
─────────────────────────────────────────────────────────────────────────────
Validates FFI boundary state serialization, text ingestion loops, memory-to-disk 
shuttle mechanisms, and error propagation paths across Rust and Python boundaries.
"""

import os
import pytest
from pathlib import Path

# Safely resolve the native submodule layout compiled via PyO3
try:
    import fintally_chatbot as core_backend
except ImportError:
    from fintally_chatbot import core as core_backend


# ── Internal Dynamic Submodule Resolution Helpers ───────────────────────────

def _get_rag_class():
    """
    Helper to locate the exposed PyO3 struct inside the nested `rag` submodule space.
    """
    assert hasattr(core_backend, "rag"), (
        f"The compiled Rust library is missing the 'rag' submodule! "
        f"Exposed top-level components are: {dir(core_backend)}"
    )
    
    rag_submodule = core_backend.rag
    possible_names = ["RagService", "RagEngine", "PyRagService", "PyRagEngine", "rag_service", "rag_engine"]
    
    for name in possible_names:
        if hasattr(rag_submodule, name):
            return getattr(rag_submodule, name)
            
    exposed_sub = dir(rag_submodule)
    raise AttributeError(
        f"Could not find a valid RAG class wrapper inside the 'fintally_chatbot.rag' submodule.\n"
        f"Exposed attributes found inside the sub-namespace were: {exposed_sub}"
    )


def _get_method(instance, preferred_name: str):
    """
    Helper to extract a bound method whether it's named snake_case or camelCase.
    """
    variations = [preferred_name, preferred_name.lower(), preferred_name.replace("_", "")]
    for var in variations:
        if hasattr(instance, var):
            return getattr(instance, var)
    
    raise AttributeError(
        f"Method variant for '{preferred_name}' not found on {type(instance)}.\n"
        f"Available attributes: {dir(instance)}"
    )


# ── Test Fixtures ────────────────────────────────────────────────────────────

@pytest.fixture
def temp_workspace_paths(tmp_path: Path):
    """
    Allocates isolated temporary file system paths for testing persistence
    safely across integration sweeps.
    """
    index_file = tmp_path / "test_workspace.usearch"
    chunks_file = tmp_path / "test_workspace_chunks.json"
    
    return {
        "index_path": str(index_file),
        "chunks_path": str(chunks_file),
        "root_dir": tmp_path
    }


@pytest.fixture
def mock_finance_file(tmp_path: Path):
    """
    Generates a realistic text sample document layout on physical disk
    to exercise the ingestion chunk-splitting pipeline blocks.
    """
    file_path = tmp_path / "financial_report.txt"
    content = (
        "Fintally Balance Sheet Records 2026.\n"
        "Total Cash Flow Liquidity Assets: $450,000 USD.\n"
        "Current outstanding liability amortization profile tracking within parameters.\n"
        "Emergency reserve buffer allocations verified matching policy requirements.\n"
        "Operational cost tracking reflects zero critical deficit alerts."
    )
    file_path.write_text(content, encoding="utf-8")
    return str(file_path)


# ── Lifecycle & Ingestion Tests ───────────────────────────────────────────────

class TestRagEngineInitialization:
    
    def test_default_constructor_instantiation(self):
        """Validates instantiation topology matches signature option boundaries."""
        RagEngineClass = _get_rag_class()
        engine = RagEngineClass(200, 40, 384, 0.7, 5, None, None)
        assert engine is not None
        
    def test_invalid_parameters_do_not_panic(self):
        """Verifies boundary parameter validation errors bubble up safely without segmentation faults."""
        RagEngineClass = _get_rag_class()
        # Test extreme alpha out-of-bounds (Should be clamped inside Rust Builder layout)
        engine = RagEngineClass(200, 40, 384, 5.5, 5, None, None)
        assert engine is not None


class TestRagEngineIngestionPipeline:

    def test_successful_file_ingestion(self, mock_finance_file):
        """Validates PyDict conversion shape coming across PyO3 layer from Rust."""
        RagEngineClass = _get_rag_class()
        engine = RagEngineClass(50, 10, 384, 0.7, 5, None, None)
        
        ingest_fn = _get_method(engine, "ingest_file")
        response = ingest_fn(mock_finance_file)
        
        # Verify structure mapped from PyDict native generation block
        assert isinstance(response, dict)
        
        doc_key = "document_id" if "document_id" in response else "document"
        success_key = "success" if "success" in response else "is_success"
        chunks_key = "chunks_count" if "chunks_count" in response else "chunks"
        
        assert doc_key in response
        assert response[success_key] is True
        assert response[chunks_key] > 0

    def test_ingest_nonexistent_file_raises_runtime_error(self):
        """Verifies native errors map to PyRuntimeError blocks on Python boundary."""
        RagEngineClass = _get_rag_class()
        engine = RagEngineClass(200, 40, 384, 0.7, 5, None, None)
        ingest_fn = _get_method(engine, "ingest_file")
        
        with pytest.raises(RuntimeError):
            ingest_fn("C:/Missing/Path/To/FakeFile.txt")


# ── Query Layer & Context Alignment Tests ─────────────────────────────────────

class TestRagEngineQueryLayer:

    def test_end_to_end_query_retrieval_shapes(self, mock_finance_file):
        """Validates dictionary structures, score filtering, and matching lists formatting."""
        RagEngineClass = _get_rag_class()
        engine = RagEngineClass(100, 20, 384, 0.7, 5, None, None)
        
        ingest_fn = _get_method(engine, "ingest_file")
        query_fn = _get_method(engine, "query")
        
        ingest_fn(mock_finance_file)
        
        # Execute Query Pass
        query_result = query_fn("What is the total cash flow liquidity asset profile?", 3, 0.0, None)
        
        assert isinstance(query_result, dict)
        
        context_key = "context_block" if "context_block" in query_result else "context"
        assert context_key in query_result
        assert isinstance(query_result[context_key], str)
        assert "matches" in query_result
        
        # Verify nested PyList conversion of chunk mappings
        matches = query_result["matches"]
        assert isinstance(matches, list)
        assert len(matches) > 0
        
        first_match = matches[0]
        assert "chunk_id" in first_match or "id" in first_match
        assert "score" in first_match

    def test_query_filter_map_conversion(self, mock_finance_file):
        """Validates that a Python dict safely transitions into Rust BTreeMap filters."""
        RagEngineClass = _get_rag_class()
        engine = RagEngineClass(100, 20, 384, 0.7, 5, None, None)
        
        ingest_fn = _get_method(engine, "ingest_file")
        query_fn = _get_method(engine, "query")
        
        ingest_fn(mock_finance_file)
        
        # Inject standard python dictionary structure mapping to filters
        search_filters = {"department": "finance", "confidentiality": "high"}
        
        result = query_fn("Liquidity assets", 5, 0.0, search_filters)
        assert "matches" in result


# ── Disk I/O Snapshot Persistence Tests ───────────────────────────────────────

class TestRagEnginePersistenceLifecycle:

    def test_explicit_save_and_load_roundtrip(self, mock_finance_file, temp_workspace_paths):
        """
        Validates the serialization integrity of the vector layout matrices
        and metadata caches using cold disk restores.
        """
        idx_p = temp_workspace_paths["index_path"]
        chk_p = temp_workspace_paths["chunks_path"]
        
        RagEngineClass = _get_rag_class()
        
        # ── Step 1: Initialize, Ingest, and Serialize down to storage
        # FIXED: Pass explicitly as keyword arguments to avoid positional mismatch bugs
        writer_engine = RagEngineClass(
            chunk_size=200,
            chunk_overlap=40,
            dimensions=384,
            alpha=0.7,
            default_top_k=5,
            index_path=idx_p,
            chunks_path=chk_p
        )
        
        ingest_fn = _get_method(writer_engine, "ingest_file")
        save_fn = _get_method(writer_engine, "save_to_disk")
        
        ingest_fn(mock_finance_file)
        save_fn()
        
        # Hard verification that binary files exist on the filesystem footprint
        assert os.path.exists(idx_p) is True
        assert os.path.exists(chk_p) is True

        # ── Step 2: Spin up a cold clean engine instance pointing to the same files
        # FIXED: Matching exact keywords here as well
        reader_engine = RagEngineClass(
            chunk_size=200,
            chunk_overlap=40,
            dimensions=384,
            alpha=0.7,
            default_top_k=5,
            index_path=idx_p,
            chunks_path=chk_p
        )
        
        load_fn = _get_method(reader_engine, "load_from_disk")
        query_fn = _get_method(reader_engine, "query")
        
        # Load snapshot maps explicitly
        load_fn()
        
        # Execute validation query against restored database state
        restored_query = query_fn("Total Cash Flow Liquidity Assets", 1, 0.0, None)
        
        context_key = "context_block" if "context_block" in restored_query else "context"
        assert restored_query[context_key] != ""

    def test_load_missing_files_bubbles_io_exception(self):
        """Verifies filesystem missing target snapshots route clean structural exceptions."""
        RagEngineClass = _get_rag_class()
        engine = RagEngineClass(200, 40, 384, 0.7, 5, "C:/Missing/invalid_file.usearch", "C:/Missing/invalid_chunks.json")
        
        load_fn = _get_method(engine, "load_from_disk")
        
        with pytest.raises(RuntimeError):
            load_fn()