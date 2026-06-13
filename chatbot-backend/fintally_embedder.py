# chatbot-backend/fintally_embedder.py
"""
Fintally Core Production Integration Layer - GGUF Engine (Memory Optimized)
─────────────────────────────────────────────────────────────────────────────
Loads your existing local GGUF model binary using llama_cpp and runs 
real mathematical 384-dimension vector extractions with safety restrictions
tailored specifically for limited hardware specs.
"""

import os
import threading
from pathlib import Path
from llama_cpp import Llama

# Identify project root path safely relative to this file's footprint
_CURRENT_DIR = Path(__file__).resolve().parent
_MODEL_PATH = _CURRENT_DIR / "llm_models" / "embedding" / "bge-small-en-v1.5-q4_k_m.gguf"

# Global lazy cache for the model instance to handle repeated PyO3 executions efficiently
_EMBED_MODEL = None
_CACHE_LOCK = threading.Lock()

# llama.cpp instances are NOT thread-safe. This lock guarantees that concurrent 
# embeddings requests from the Rust RAG pipeline execute in a safe sequence.
_EMBED_INFERENCE_LOCK = threading.Lock()

# ========================= MEMORY & HARDWARE TUNING =========================
# Match the core system parameters used by python_llama.py
DEFAULT_N_THREADS = int(os.getenv("LLAMA_N_THREADS", max(1, (os.cpu_count() or 2) - 1)))


def _get_model_instance() -> Llama:
    """
    Ensures the underlying GGUF model is safely loaded into memory lazily.
    Thread-safe initialization via _cache_lock.
    """
    global _EMBED_MODEL
    with _CACHE_LOCK:
        if _EMBED_MODEL is None:
            if not _MODEL_PATH.exists():
                raise FileNotFoundError(
                    f"Real embedding model binary not found at target layout path: {_MODEL_PATH.resolve()}"
                )
            
            print(f"[fintally_embedder] Loading BGE Embedding Model | threads={DEFAULT_N_THREADS} | mmap=ENABLED")
            
            # Initialize llama_cpp with optimized hardware flags
            _EMBED_MODEL = Llama(
                model_path=str(_MODEL_PATH),
                embedding=True,
                verbose=False,       # Keeps your Rust cargo test output clean of llama C logs
                n_ctx=512,           # BGE small standard sequence input ceiling context boundary
                n_threads=DEFAULT_N_THREADS, # Prevents Celeron CPU thread-thrashing core locks
                use_mmap=True,       # Memory map model file to avoid heavy RAM load and cold starts
                n_gpu_layers=0       # Explicitly run on CPU architecture only
            )
    return _EMBED_MODEL


def get_onnx_embedding(text: str) -> list[float]:
    """
    Processes raw text strings through the local GGUF model file.
    Returns a real, mathematically evaluated 384-dimensional array of floats.
    """
    if not text or not text.strip():
        raise ValueError("Inference execution exception: Empty or blank text buffer passed.")

    model = _get_model_instance()
    
    # Run structural embedding generation inference vector pass safely behind a lock
    with _EMBED_INFERENCE_LOCK:
        raw_output = model.create_embedding(text)
    
    # Extract raw float vector array output safely
    vector = raw_output["data"][0]["embedding"]
    
    # BGE-small-en output check validation safety guard rails
    if len(vector) != 384:
        raise ValueError(f"Model dimension topology mismatch. Expected 384, generated {len(vector)}")
        
    return vector