"""
Fintally RAG + LLM Streaming End-to-End Integration Tests
─────────────────────────────────────────────────────────────────────────────
Validates the full production pipeline: Ingesting a document, searching context 
via the Rust vector layer, augmenting an LLM prompt, and verifying token delivery.
"""

import asyncio
import importlib.util
import pytest
from pathlib import Path

# ── ENV GUARD: Detect extension and runtime status cleanly ──
try:
    import fintally_chatbot as fc
    _EXTENSION_AVAILABLE = True
except ImportError:
    _EXTENSION_AVAILABLE = False

# Only inspect python_llama spec structure if the extension successfully loaded
_PYTHON_LLAMA_AVAILABLE = (
    importlib.util.find_spec("python_llama") is not None 
    if _EXTENSION_AVAILABLE else False
)


# --- Internal Dynamic Resolution Helpers ---

def _get_backend_context():
    """Ensures module availability without throwing unhandled exceptions at top level."""
    if not _EXTENSION_AVAILABLE:
        pytest.skip(
            "fintally_chatbot extension not built. Run `maturin develop` first.",
            allow_module_level=True
        )
    return fc


def _get_rag_class():
    backend = _get_backend_context()
    assert hasattr(backend, "rag"), "The compiled Rust library is missing the 'rag' submodule!"
    rag_submodule = backend.rag
    possible_names = ["RagService", "RagEngine", "PyRagService", "PyRagEngine"]
    for name in possible_names:
        if hasattr(rag_submodule, name):
            return getattr(rag_submodule, name)
    raise AttributeError("Could not locate a valid RAG class wrapper inside 'fintally_chatbot.rag'.")


def _get_method(instance, preferred_name: str):
    variations = [preferred_name, preferred_name.lower(), preferred_name.replace("_", "")]
    for var in variations:
        if hasattr(instance, var):
            return getattr(instance, var)
    raise AttributeError(f"Method variant for '{preferred_name}' not found on {type(instance)}.")


# --- Document Ingestion Fixture ---

@pytest.fixture
def mock_real_world_doc(tmp_path: Path) -> str:
    """Generates a physical document simulating real financial data."""
    file_path = tmp_path / "company_performance_2026.txt"
    content = (
        "Fintally Enterprise Analytics Platform Q2 Report.\n"
        "Net revenue scaled to $840,000 USD with a gross operating margin of 62%.\n"
        "Strategic reserves hold $150,000 USD exclusively allocated for R&D infrastructure.\n"
        "Operational overhead costs reduced by 12% via optimized automated telemetry processing."
    )
    file_path.write_text(content, encoding="utf-8")
    return str(file_path)


# --- End-to-End Integration Test Suite ---

@pytest.mark.integration
@pytest.mark.skipif(
    not _PYTHON_LLAMA_AVAILABLE,
    reason="python_llama not importable or model files missing from active environment context",
)
class TestRagLlmStreamIntegration:

    def test_e2e_rag_to_llm_stream_pipeline(self, mock_real_world_doc):
        """
        Validates the full RAG-to-LLM lifecycle:
        1. Ingest document into vector space.
        2. Query vector space for relevant context chunks.
        3. Inject retrieved chunks as context directly into the LLM prompt.
        4. Read token chunks back out of the async streaming channel.
        """
        # Ensure extension exists before entering execution thread paths
        backend = _get_backend_context()

        # ── Step 1: Initialize and Ingest Context into the RAG Engine ──
        RagEngineClass = _get_rag_class()
        rag_engine = RagEngineClass(
            chunk_size=120,
            chunk_overlap=20,
            dimensions=384,
            alpha=0.5,
            default_top_k=2,
            index_path=None,
            chunks_path=None
        )
        
        ingest_fn = _get_method(rag_engine, "ingest_file")
        query_fn = _get_method(rag_engine, "query")
        
        ingest_result = ingest_fn(mock_real_world_doc)
        assert ingest_result["success"] is True, "RAG initialization ingestion failed."

        # ── Step 2: Query Context for the User Prompt ──
        user_question = "What was the gross operating margin and net revenue?"
        rag_response = query_fn(user_question, 2, 0.0, None)
        
        context_key = "context_block" if "context_block" in rag_response else "context"
        retrieved_context = rag_response.get(context_key, "")
        
        # Verify that our RAG engine extracted the correct text nodes before sending to LLM
        assert "62%" in retrieved_context
        assert "$840,000" in retrieved_context

        # ── Step 3: Initialize the Streaming LLM ──
        assert hasattr(backend, "llm"), "No llm submodule found in fintally_chatbot."
        llm_mod = backend.llm
        llm = llm_mod.create_llm("tinyllama", 16)  # Light budget token space for CPU runs

        # ── Step 4: Execute the Combined Async Stream Handshake ──
        async def execute_rag_augmented_stream():
            # Build production system context prompt injecting our real RAG findings
            system_context = (
                f"You are Fintally's internal financial AI assistant.\n"
                f"Use the following verified context to answer questions:\n"
                f"{retrieved_context}"
            )
            
            # Open the Rust cross-beam token channel
            stream_obj = await llm.stream(user_question, context=system_context)
            assert stream_obj is not None, "Failed to initialize native stream token channel."
            
            tokens_received = []
            
            # Read streaming tokens asynchronously as they flow across the thread barrier
            async for token in stream_obj:
                tokens_received.append(token)
                
                # Performance optimization: stop early after collecting enough text
                # to save CPU cycles on local hardware
                if len(tokens_received) >= 4:
                    stream_obj.cancel()
                    break
            
            # Cleanly drain any leftover buffer to avoid hanging the background loop
            async for _ in stream_obj:
                pass
                
            return tokens_received

        # Run the async orchestration loop
        streamed_tokens = asyncio.run(execute_rag_augmented_stream())

        # ── Step 5: Final Pipeline Verification ──
        assert len(streamed_tokens) > 0, "LLM failed to yield any tokens through the augmented RAG pipeline."
        for token in streamed_tokens:
            assert isinstance(token, str), f"Expected token stream to be string fragments, got: {type(token)}"