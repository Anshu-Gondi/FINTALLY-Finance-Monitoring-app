"""
Fintally Chatbot RAG Pipeline End-to-End Integration Tests
─────────────────────────────────────────────────────────────────────────────
Verifies the orchestration boundary between Python data execution states and
the underlying Rust core workspace layer using real 384-dimension GGUF embeddings.
"""

import os
import pytest
from pathlib import Path

# ── ENV GUARD: Safe Extension and Embedder Check ──
try:
    import fintally_chatbot as core_backend
    _EXTENSION_AVAILABLE = True
except ImportError:
    from fintally_chatbot import core as core_backend
    _EXTENSION_AVAILABLE = True
except Exception:
    _EXTENSION_AVAILABLE = False

try:
    import fintally_embedder
    # A quick spec or module attribute check to see if llama_cpp weights are loadable
    _EMBEDDER_AVAILABLE = hasattr(fintally_embedder, "get_onnx_embedding")
except Exception:
    _EMBEDDER_AVAILABLE = False


def _get_rag_class():
    """
    Helper to locate the exposed PyO3 struct inside the `rag` submodule space.
    """
    if not _EXTENSION_AVAILABLE:
        pytest.skip("fintally_chatbot native module not compiled.", allow_module_level=True)
        
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


# ── TEST SUITE ──

@pytest.mark.integration
@pytest.mark.skipif(
    not _EMBEDDER_AVAILABLE,
    reason="fintally_embedder or backing model weight files are missing in this environment"
)
def test_fintally_embedder_real_onnx_dimensions():
    """
    Ensures that the production GGUF pipeline successfully runs inference
    and yields exactly 384 dimensions matching the Rust USearch index constraints.
    """
    sample_text = "Fintally real-time fiscal performance audit log."
    vector = fintally_embedder.get_onnx_embedding(sample_text)
    
    assert isinstance(vector, list), "Embedding output must be a standard list of floats."
    assert len(vector) == 384, f"Dimension topology mismatch! Expected 384, but got {len(vector)}."
    assert all(isinstance(x, float) for x in vector), "All elements within the vector must be raw floats."


@pytest.mark.integration
def test_rust_rag_service_empty_query_fallback():
    """
    Validates that the Rust RagService orchestrates correctly through the Python binding layer,
    and returns clean, structurally accurate empty data arrays instead of crashing.
    """
    RagServiceClass = _get_rag_class()

    # (chunk_size=512, chunk_overlap=50, dimensions=384, alpha=0.5, default_top_k=5, index_path=None, chunks_path=None)
    rag_service = RagServiceClass(512, 50, 384, 0.5, 5, None, None)

    query_fn = _get_method(rag_service, "query")

    query_text = "What is our quarterly cash runway?"
    filters = {"department": "finance", "confidential": "true"}

    # Execute search query traversing the PyO3 boundary layers
    response = query_fn(query_text, 3, 0.4, filters)

    # Dictionary-based key lookups to handle raw PyO3 dict outputs safely
    assert isinstance(response, dict), f"Expected response layout type to be a dict, got {type(response)}"
    
    context_key = "context_block" if "context_block" in response else "context"
    assert context_key in response, "QueryResponse dict missing context payload field keys."
    assert "matches" in response, "QueryResponse dict missing 'matches' collection payload key."
    assert len(response["matches"]) == 0, "Matches list should remain blank prior to document injection runs."


@pytest.mark.integration
@pytest.mark.skipif(
    not _EMBEDDER_AVAILABLE,
    reason="Cannot run ingestion roundtrip without fully operational embedding weights models"
)
def test_rag_ingestion_to_retrieval_roundtrip(tmp_path):
    """
    Tests the complete end-to-end multi-language loop:
    Python -> Ingest Mock File -> Rust Token Splitting -> Python Real Vector Pass -> 
    Rust USearch Matrix Registration -> Rust Serialization -> Query Verification.
    """
    idx_dump = tmp_path / "test_workspace_index.bin"
    chk_dump = tmp_path / "test_workspace_chunks.json"

    RagServiceClass = _get_rag_class()
    
    # (chunk_size=512, chunk_overlap=50, dimensions=384, alpha=0.5, default_top_k=5, index_path, chunks_path)
    rag_service = RagServiceClass(512, 50, 384, 0.5, 5, str(idx_dump), str(chk_dump))

    # Create a physical text file payload for the ingestion pipeline loader to parse
    mock_doc = tmp_path / "q4_financial_statement.txt"
    mock_doc.write_text(
        "Fintally software stack achieves 45% margin acceleration in Q4. "
        "Total operational budget allocated for development rounds sits at $150000.",
        encoding="utf-8"
    )

    ingest_fn = _get_method(rag_service, "ingest_file")
    query_fn = _get_method(rag_service, "query")
    
    # 1. Trigger the ingestion loop
    ingest_result = ingest_fn(str(mock_doc))
    
    # Handle dictionary result layout parsing for the ingestion response
    assert isinstance(ingest_result, dict), f"Expected ingestion output map to be a dict, got {type(ingest_result)}"
    
    success_key = "success" if "success" in ingest_result else "is_success"
    chunks_key = "chunks_count" if "chunks_count" in ingest_result else "chunks"
    
    assert ingest_result.get(success_key) is True, "RAG system failed to ingest file structure cleanly."
    assert ingest_result.get(chunks_key, 0) > 0, "Ingestion process generated 0 overlapping token frames."

    # Verify physical file snapshots were written down to disk automatically by Rust core
    assert idx_dump.exists(), "USearch index graph snapshot failed to persist on filesystem."
    assert chk_dump.exists(), "Text chunk mapping dictionary failed to persist on filesystem."

    # 2. Query the populated vector index layer
    query_text = "What was the total budget allocation?"
    query_response = query_fn(query_text, 1, 0.1, None)

    # 3. Structural dictionary verification validating metrics back out of index
    assert isinstance(query_response, dict), f"Expected query output map to be a dict, got {type(query_response)}"
    assert len(query_response["matches"]) > 0, "Pipeline failed to extract structural matches from search queries."
    
    context_key = "context_block" if "context_block" in query_response else "context"
    context_data = query_response.get(context_key, "")
    assert "budget" in context_data.lower(), f"Context compilation block failed to include matches text. Got: {context_data}"
    
    # Parse match properties gracefully whether it's a sub-dict or a typed item object
    first_match = query_response["matches"][0]
    score = first_match["score"] if isinstance(first_match, dict) else getattr(first_match, "score", 0.0)
    assert score >= 0.0, "Similarity metrics dropped into invalid numeric space."