"""
Fintally RAG Multi-Format Document Ingestion Integration Tests
─────────────────────────────────────────────────────────────────────────────
Validates binary file text-extraction vectors (PDF & DOCX) using PyMuPDF and python-docx,
ensuring the extracted text matches index boundaries inside the native Rust HNSW engine.
"""

import pytest
from pathlib import Path

# ── ENV GUARD: Auto-Skip entire file if external parsing libraries are missing ──
# If either dependency is missing in CI, pytest registers this as an 
# official, graceful skip rather than a compilation/collection crash.
try:
    from docx import Document
    _DOCX_AVAILABLE = True
except (ImportError, ModuleNotFoundError):
    _DOCX_AVAILABLE = False

try:
    import fitz  # PyMuPDF
    _PDF_AVAILABLE = True
except (ImportError, ModuleNotFoundError):
    _PDF_AVAILABLE = False

# Safely resolve the native submodule layout compiled via PyO3
try:
    import fintally_chatbot as core_backend
except ImportError:
    from fintally_chatbot import core as core_backend

# --- Dynamic Helper Utilities ---

def _get_rag_class():
    rag_submodule = core_backend.rag
    possible_names = ["RagService", "RagEngine", "PyRagService", "PyRagEngine"]
    for name in possible_names:
        if hasattr(rag_submodule, name):
            return getattr(rag_submodule, name)
    raise AttributeError("Could not locate native RAG Engine struct wrapper.")

def _get_method(instance, preferred_name: str):
    variations = [preferred_name, preferred_name.lower(), preferred_name.replace("_", "")]
    for var in variations:
        if hasattr(instance, var):
            return getattr(instance, var)
    raise AttributeError(f"Method '{preferred_name}' missing on native engine wrapper.")


# --- Binary File Generation Fixtures ---

@pytest.fixture
def sample_pdf_document(tmp_path: Path) -> str:
    """
    Generates a valid, physical PDF asset using PyMuPDF (fitz).
    Inserts a unique financial validation string into a clean page layout.
    """
    pdf_path = tmp_path / "quarterly_invoice_leakage.pdf"
    
    # Initialize a clean PDF document stream via PyMuPDF
    doc = fitz.open()
    page = doc.new_page()
    
    # Insert target text string block for extraction tests
    text_position = fitz.Point(50, 50)
    page.insert_text(text_position, "Target Leakage Code: FX-99281. Total deficit detected: $14,200 USD.", fontsize=11)
    
    doc.save(str(pdf_path))
    doc.close()
    return str(pdf_path)


@pytest.fixture
def sample_docx_document(tmp_path: Path) -> str:
    """
    Generates a physical DOCX OpenXML file layout on disk containing text nodes.
    """
    docx_path = tmp_path / "corporate_policy_framework.docx"
    
    doc = Document()
    doc.add_heading("Fintally Strategic Capital Framework", level=1)
    doc.add_paragraph(
        "This policy restricts cryptocurrency arbitrage limits. "
        "The absolute maximum corporate exposure allocation cap is set to 8% of core equity buffers."
    )
    doc.save(str(docx_path))
    
    return str(docx_path)


# --- Text Ingestion Adapters (Using your pre-installed PyMuPDF layer) ---

def extract_text_from_pdf(file_path: str) -> str:
    """Extracts plain text layers from a PDF binary using ultra-fast PyMuPDF loops."""
    doc = fitz.open(file_path)
    text_accumulator = []
    
    for page in doc:
        text_accumulator.append(page.get_text())
        
    doc.close()
    return "\n".join(text_accumulator)


def extract_text_from_docx(file_path: str) -> str:
    """Extracts text elements from paragraphs inside a DOCX document open XML structure."""
    doc = Document(file_path)
    return "\n".join([p.text for p in doc.paragraphs])


# --- The Integration Test Suite ---

@pytest.mark.integration
class TestMultiFormatIngestionPipeline:
    
    @pytest.mark.skipif(not _PDF_AVAILABLE, reason="PyMuPDF (fitz) is not installed in this environment context")
    def test_pdf_extraction_to_rust_rag_roundtrip(self, sample_pdf_document, tmp_path):
        """
        Validates the text parsed from a binary PDF via PyMuPDF cleanly bridges into 
        the Rust engine and is completely searchable via the HNSW vector layer.
        """
        # 1. Read and translate the PDF layout using PyMuPDF
        plain_text = extract_text_from_pdf(sample_pdf_document)
        assert "FX-99281" in plain_text  # Verify PyMuPDF extraction worked
        
        # Save the extracted plain text file alongside for Rust to consume safely
        text_mirror_path = Path(sample_pdf_document).with_suffix(".txt")
        text_mirror_path.write_text(plain_text, encoding="utf-8")

        # 2. Fire up the Native Engine Instance
        RagEngineClass = _get_rag_class()
        engine = RagEngineClass(
            chunk_size=100,
            chunk_overlap=20,
            dimensions=384,
            alpha=0.7,
            default_top_k=3,
            index_path=None,
            chunks_path=None
        )
        
        ingest_fn = _get_method(engine, "ingest_file")
        query_fn = _get_method(engine, "query")
        
        # 3. Ingest via Rust Pipeline
        ingest_result = ingest_fn(str(text_mirror_path))
        assert ingest_result["success"] is True

        # 4. Search the vector space for the data born inside that PDF
        query_result = query_fn("What is the leakage code identifier?", 1, 0.0, None)
        assert "FX-99281" in query_result["context_block"]

    @pytest.mark.skipif(not _DOCX_AVAILABLE, reason="python-docx library is missing or unconfigured in CI")
    def test_docx_extraction_to_rust_rag_roundtrip(self, sample_docx_document, tmp_path):
        """
        Validates Word .docx structures map smoothly through Python extraction models 
        and successfully embed inside our hyper-light local vector index layer.
        """
        # 1. Parse word elements
        plain_text = extract_text_from_docx(sample_docx_document)
        assert "cryptocurrency" in plain_text
        
        text_mirror_path = Path(sample_docx_document).with_suffix(".txt")
        text_mirror_path.write_text(plain_text, encoding="utf-8")

        # 2. Initialize Engine
        RagEngineClass = _get_rag_class()
        engine = RagEngineClass(
            chunk_size=100,
            chunk_overlap=20,
            dimensions=384,
            alpha=0.7,
            default_top_k=2,
            index_path=None,
            chunks_path=None
        )
        
        ingest_fn = _get_method(engine, "ingest_file")
        query_fn = _get_method(engine, "query")
        
        # 3. Ingest and query index matrix elements
        ingest_fn(str(text_mirror_path))
        
        query_result = query_fn("What is the maximum corporate cryptocurrency risk limit cap?", 1, 0.0, None)
        assert "8%" in query_result["context_block"]