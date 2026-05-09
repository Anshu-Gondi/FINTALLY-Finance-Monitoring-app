"""
python_llama.py - Memory Optimized for Low-End Hardware
"""

from llama_cpp import Llama
import os
import threading

_BASE_DIR = os.path.dirname(os.path.abspath(__file__))

MODEL_PATHS = {
    "tinyllama": os.path.join(_BASE_DIR, "llm_models", "tinyllama-1.1b-chat-v1.0.Q4_0.gguf"),
    "phi2": os.path.join(_BASE_DIR, "llm_models", "phi-2.Q4_K_M.gguf"),
}

# Global cache
_model_cache: dict = {}
_cache_lock = threading.Lock()
_stop_flag = threading.Event()

# ========================= MEMORY TUNING =========================
DEFAULT_N_CTX = int(os.getenv("LLAMA_N_CTX", 1024))          # Reduced from 2048
DEFAULT_N_BATCH = int(os.getenv("LLAMA_N_BATCH", 512))
DEFAULT_N_THREADS = int(os.getenv("LLAMA_N_THREADS", 2))

def _get_model(model_name: str) -> Llama:
    """Load model with memory-optimized settings."""
    with _cache_lock:
        if model_name not in _model_cache:
            path = MODEL_PATHS.get(model_name)
            if path is None or not os.path.exists(path):
                raise FileNotFoundError(f"Model not found: {path}")

            print(f"Loading model: {model_name} | n_ctx={DEFAULT_N_CTX} | n_batch={DEFAULT_N_BATCH}")

            _model_cache[model_name] = Llama(
                model_path=path,
                n_ctx=DEFAULT_N_CTX,           # Most important for RAM
                n_batch=DEFAULT_N_BATCH,       # Controls memory spikes
                n_threads=DEFAULT_N_THREADS,
                n_gpu_layers=0,                # CPU only
                verbose=False,
                # Additional memory saving options:
                logits_all=False,              # Don't keep all logits
                embedding=False,               # Disable if not needed
                # use_mmap=True,               # Default is good
            )
    return _model_cache[model_name]


_active_model_name: str = "tinyllama"
_active_max_tokens: int = 512


def init(model_name: str = "tinyllama", max_tokens: int = 512) -> None:
    """Pre-load model at startup."""
    global _active_model_name, _active_max_tokens
    _active_model_name = model_name
    _active_max_tokens = max_tokens
    _get_model(model_name)   # Warm-up
    print(f"✅ LLM initialized: {model_name} (n_ctx={DEFAULT_N_CTX})")


def stream_generate(prompt: str, max_tokens: int):
    """Main function called by Rust."""
    _stop_flag.clear()

    model = _get_model(_active_model_name)

    formatted = f"<|system|>\nYou are a helpful personal finance assistant.\n</s>\n<|user|>\n{prompt}\n</s>\n<|assistant|>\n"

    stream = model(
        formatted,
        max_tokens=max_tokens or _active_max_tokens,
        stream=True,
        stop=["</s>", "<|user|>", "<|system|>"],
        echo=False,
        temperature=0.7,
    )

    for chunk in stream:
        if _stop_flag.is_set():
            break
        token = chunk["choices"][0]["text"]
        if token:
            yield token


def stop() -> None:
    """Cancel generation."""
    _stop_flag.set()