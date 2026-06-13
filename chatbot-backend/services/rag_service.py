# services/rag_service.py
"""
FinTally RAG Core Service Subsystem
──────────────────────────────────────────────────────────────────────────────
Handles document ingestion, fast text parsing (PDF/DOCX), and vector similarity 
searches using the native Rust HNSW index engine wrapper combined with Polars.
"""

import os
import logging
from pathlib import Path
from typing import Optional, Dict, Any

import fitz  # PyMuPDF
import polars as pl  # High-speed data layout engine
from docx import Document

try:
    import fintally_chatbot as fc
except ImportError:
    fc = None

logger = logging.getLogger(__name__)


class RagService:
    def __init__(self: "RagService") -> None:
        self.engine_class = self._resolve_native_rag_class()
        # Initialize an in-memory Polars storage container for instant textual alignment
        self.knowledge_vault = pl.DataFrame(
            schema={"chunk_id": pl.Int64, "source_file": pl.String, "text_content": pl.String}
        )
        self._chunk_id_counter = 0
        
    def _resolve_native_rag_class(self: "RagService") -> Optional[type]:
        if not fc or not hasattr(fc, "rag"):
            logger.error("fintally_chatbot extension or 'rag' submodule missing.")
            return None
        
        for name in ["RagService", "RagEngine", "PyRagService", "PyRagEngine"]:
            if hasattr(fc.rag, name):
                return getattr(fc.rag, name)
        return None

    def _get_engine_instance(self: "RagService") -> Optional[Any]:
        if not self.engine_class:
            return None
        try:
            # Create a physical directory to hold your AI brains on the disk
            storage_dir = os.path.join(os.getcwd(), "vector_storage")
            os.makedirs(storage_dir, exist_ok=True)
            
            return self.engine_class(
                chunk_size=120,
                chunk_overlap=20,
                dimensions=384,
                alpha=0.5,
                default_top_k=2,
                index_path=os.path.join(storage_dir, "hnsw_index.bin"),  
                chunks_path=os.path.join(storage_dir, "text_chunks.db")
            )
        except Exception as e:
            logger.error(f"Failed to instantiate native RagEngine: {e}")
            return None

    def extract_text_from_pdf(self: "RagService", file_path: str) -> str:
        """Extracts plain text matrices from binary PDF streams via PyMuPDF."""
        try:
            doc = fitz.open(file_path)
            text_accumulator = [page.get_text() for page in doc]
            doc.close()
            return "\n".join(text_accumulator)
        except Exception as e:
            logger.error(f"PyMuPDF failed parsing document {file_path}: {e}")
            raise RuntimeError(f"PDF extraction error: {e}")

    def extract_text_from_docx(self: "RagService", file_path: str) -> str:
        """Extracts text components from DOCX file structures."""
        try:
            doc = Document(file_path)
            return "\n".join([p.text for p in doc.paragraphs])
        except Exception as e:
            logger.error(f"python-docx failed parsing document {file_path}: {e}")
            raise RuntimeError(f"Docx extraction error: {e}")

    async def ingest_document(self: "RagService", file_path: str) -> Dict[str, Any]:
        """
        Parses PDF/DOCX layouts down to raw text strings, caches chunks inside 
        a Polars DataFrame, and synchronizes the embeddings directly with Rust.
        """
        path_obj = Path(file_path)
        ext = path_obj.suffix.lower()
        
        # 1. Parse document text content using existing specialized engines
        if ext == ".pdf":
            plain_text = self.extract_text_from_pdf(file_path)
        elif ext == ".docx":
            plain_text = self.extract_text_from_docx(file_path)
        elif ext in [".txt", ".json", ".csv"]:
            plain_text = path_obj.read_text(encoding="utf-8")
        else:
            return {"success": False, "error": f"Unsupported format: {ext}"}

        if not plain_text.strip():
            return {"success": False, "error": "Document contains no readable text layers."}

        # 2. Simple clean string splitting array to track text chunks inside Polars
        raw_chunks = [c.strip() for c in plain_text.split("\n\n") if c.strip()]
        
        new_records = []
        for chunk in raw_chunks:
            new_records.append({
                "chunk_id": self._chunk_id_counter,
                "source_file": path_obj.name,
                "text_content": chunk
            })
            self._chunk_id_counter += 1

        # Append instantly to our Polars memory store using fast Arrow arrays
        if new_records:
            append_df = pl.DataFrame(new_records)
            self.knowledge_vault = pl.concat([self.knowledge_vault, append_df], how="vertical")

        # 3. Write a transient plain text companion file for the native Rust core to ingest
        mirror_txt_path = path_obj.with_suffix(f"{path_obj.suffix}.txt")
        mirror_txt_path.write_text(plain_text, encoding="utf-8")
        
        engine = self._get_engine_instance()
        if not engine:
            return {"success": False, "error": "Native engine unavailable"}
            
        try:
            ingest_fn = getattr(engine, "ingest_file", getattr(engine, "ingestfile", None))
            if not ingest_fn:
                return {"success": False, "error": "Native engine missing ingestion signature"}
                
            result = ingest_fn(str(mirror_txt_path))
            return {"success": True, "payload": result}
        except Exception as e:
            logger.error(f"Native core ingestion failed: {e}")
            return {"success": False, "error": str(e)}
        finally:
            if mirror_txt_path.exists():
                try:
                    os.remove(mirror_txt_path)
                except Exception:
                    pass
    
    # ── Add this method directly inside your existing RagService class ──
    async def sync_and_reindex_trusted_sources(self: "RagService") -> None:
        """
        Pulls newly uploaded Indian Government tax amendments from Google Drive 
        and updates both Polars DataFrames and the native Rust vector database layout.
        """
        from services.utils import download_llm_docs_from_drive
        
        target_dir = "./trusted_docs_source"
        try:
            new_files = download_llm_docs_from_drive(target_dir)
            if not new_files:
                logger.info("No new tax documents detected on Google Drive. RAG indexes are current.")
                return

            logger.info(f"Detected {len(new_files)} new files from Drive. Running ingest routines...")
            for file_path in new_files:
                # Call your existing async ingestion pipeline
                result = await self.ingest_document(file_path)
                if result.get("success"):
                    logger.info(f"Successfully processed and embedded into Rust: {os.path.basename(file_path)}")
                else:
                    logger.error(f"Failed parsing {os.path.basename(file_path)}: {result.get('error')}")
                    
        except Exception as e:
            logger.error(f"CRITICAL: Failed tracking trusted source migrations: {e}", exc_info=True)

    async def search_knowledge(self: "RagService", query_text: str, limit: int = 2) -> str:
        """
        Queries the native vector index to retrieve close vectors, then uses Polars 
        to immediately filter and return the corresponding string contexts.
        """
        engine = self._get_engine_instance()
        if not engine or self.knowledge_vault.is_empty():
            return ""
            
        try:
            query_fn = getattr(engine, "query", None)
            if not query_fn:
                return ""
                
            # Perform rapid HNSW similarity search across vector borders
            response = query_fn(query_text, limit, 0.0, None)
            
            # Extract specific chunk index IDs returned from Rust engine matches
            matched_ids = response.get("matched_ids", [])
            if not matched_ids:
                # Fallback to direct raw string block response if engine returns text directly
                context_key = "context_block" if "context_block" in response else "context"
                if context_key in response:
                    return response[context_key]
                return ""

            # Use Polars' blazing-fast Rust implementation to slice the exact string hits out
            matching_chunks = self.knowledge_vault.filter(pl.col("chunk_id").is_in(matched_ids))
            
            text_blocks = matching_chunks["text_content"].to_list()
            return "\n\n".join(text_blocks)
            
        except Exception as e:
            logger.warning(f"Failed to query semantic vector store index space: {e}")
            return ""

# Global accessible single-instance tracker layout
rag_service = RagService()