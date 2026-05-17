"""
python_llama.py - Memory Optimized for Low-End Hardware

Design decisions and fixes:

ISSUE 1 — Per-request cancellation token (fixed)
  Each call to stream_generate() gets its own threading.Event().
  Cancellation of one request cannot accidentally cancel another.
  stop() now takes an optional generation_id returned via inference_status()
  so callers cancel exactly what they started.

ISSUE 2 — Lock held during entire stream (intentional, documented)
  llama.cpp is not thread-safe. _inference_lock serializes all inference.
  One request at a time. Second request blocks until first finishes.
  This is correct for single-model CPU inference on low-end hardware.

ISSUE 3 — Stop responsiveness (improved)
  Cancel event is checked before AND after each yielded chunk.
  We cannot interrupt llama.cpp mid-chunk — that is a llama.cpp limitation.
  But we catch cancellation as soon as the current chunk completes.

ISSUE 4 — Queue visibility (fixed)
  inference_status() returns "idle" or "busy" + generation_id.
  FastAPI can call this before queuing to return 503 immediately
  instead of silently blocking the client.
"""

from llama_cpp import Llama
import os
import threading
import uuid

_BASE_DIR = os.path.dirname(os.path.abspath(__file__))

MODEL_PATHS = {
    "tinyllama": os.path.join(_BASE_DIR, "llm_models", "chat", "tinyllama-1.1b-chat-v1.0.Q4_0.gguf"),
    "phi2":      os.path.join(_BASE_DIR, "llm_models", "chat", "phi-2.Q4_K_M.gguf"),
}

# ── Model cache ───────────────────────────────────────────────────────────────
_model_cache: dict = {}
_cache_lock = threading.Lock()

# ── Inference lock (Issue 2) ──────────────────────────────────────────────────
# llama.cpp Llama objects are NOT thread-safe.
# This lock ensures only one stream_generate() runs inside llama.cpp at a time.
_inference_lock = threading.Lock()

# ── Queue visibility state (Issue 4) ─────────────────────────────────────────
_inference_busy = threading.Event()        # set = busy, clear = idle
_current_generation_id: str | None = None
_inference_state_lock = threading.Lock()   # protects _current_generation_id

# ── Per-request cancellation registry (Issue 1) ───────────────────────────────
# Maps generation_id -> threading.Event (cancel signal for that request only).
_cancel_registry: dict[str, threading.Event] = {}
_registry_lock = threading.Lock()

# ========================= MEMORY TUNING =========================
DEFAULT_N_CTX     = int(os.getenv("LLAMA_N_CTX",     2048))
DEFAULT_N_BATCH   = int(os.getenv("LLAMA_N_BATCH",    512))
DEFAULT_N_THREADS = int(os.getenv("LLAMA_N_THREADS", max(1, (os.cpu_count() or 2) - 1)))

_active_model_name: str = "tinyllama"
_active_max_tokens: int = 512


# ─────────────────────────────────────────────────────────────────────────────
# Internal helpers
# ─────────────────────────────────────────────────────────────────────────────

def _get_model(model_name: str) -> Llama:
    """Load and cache model. Thread-safe via _cache_lock."""
    with _cache_lock:
        if model_name not in _model_cache:
            path = MODEL_PATHS.get(model_name)
            if path is None or not os.path.exists(path):
                raise FileNotFoundError(f"Model not found: {path!r}")

            print(f"[python_llama] Loading {model_name!r} | n_ctx={DEFAULT_N_CTX} | n_batch={DEFAULT_N_BATCH}")

            _model_cache[model_name] = Llama(
                model_path=path,
                n_ctx=DEFAULT_N_CTX,
                n_batch=DEFAULT_N_BATCH,
                n_threads=DEFAULT_N_THREADS,
                n_gpu_layers=0,
                verbose=False,
                logits_all=False,
                embedding=False,
            )
    return _model_cache[model_name]


def _register_cancel_event(generation_id: str) -> threading.Event:
    event = threading.Event()
    with _registry_lock:
        _cancel_registry[generation_id] = event
    return event


def _unregister_cancel_event(generation_id: str) -> None:
    with _registry_lock:
        _cancel_registry.pop(generation_id, None)


def _mark_busy(generation_id: str) -> None:
    global _current_generation_id
    with _inference_state_lock:
        _current_generation_id = generation_id
        _inference_busy.set()


def _mark_idle() -> None:
    global _current_generation_id
    with _inference_state_lock:
        _current_generation_id = None
        _inference_busy.clear()


# ─────────────────────────────────────────────────────────────────────────────
# Public API
# ─────────────────────────────────────────────────────────────────────────────

def init(model_name: str = "tinyllama", max_tokens: int = 512) -> None:
    """Pre-load model at startup to avoid cold-start on first request."""
    global _active_model_name, _active_max_tokens
    _active_model_name = model_name
    _active_max_tokens = max_tokens
    _get_model(model_name)
    print(f"[python_llama] Initialized {model_name!r} (n_ctx={DEFAULT_N_CTX})")


def inference_status() -> dict:
    """
    (Issue 4) Returns current inference state for queue visibility.

    FastAPI can call this before accepting a request to return 503
    immediately rather than silently blocking the client for minutes.

    Returns:
        {"status": "idle"}
        {"status": "busy", "generation_id": "<uuid>"}

    Usage in a FastAPI route:
        status = inference_status()
        if status["status"] == "busy":
            raise HTTPException(503, detail="Inference busy, try again shortly")
    """
    with _inference_state_lock:
        if _inference_busy.is_set():
            return {"status": "busy", "generation_id": _current_generation_id}
        return {"status": "idle"}


def stop(generation_id: str | None = None) -> bool:
    """
    (Issue 1 + 3) Cancel a specific generation by its ID.

    If generation_id is None, cancels whatever is currently running.
    Returns True if a signal was sent, False if nothing matched.

    Limitation (Issue 3): the cancel event is checked between chunks.
    If llama.cpp is mid-chunk internally, stop() cannot interrupt it
    until that chunk completes. This is a llama.cpp limitation.

    Usage:
        status = inference_status()
        gen_id = status.get("generation_id")
        stop(gen_id)   # cancel exactly that request
    """
    with _inference_state_lock:
        target_id = generation_id or _current_generation_id
        if target_id is None:
            return False

    with _registry_lock:
        event = _cancel_registry.get(target_id)

    if event is None:
        return False  # already finished

    event.set()
    return True


def stream_generate(prompt: str, max_tokens: int):
    """
    Main function called by Rust (PythonLlamaEngine.stream_generate).

    Each call creates its own cancel event (Issue 1), acquires the
    inference lock (Issue 2), updates visibility state (Issue 4), and
    checks cancellation before and after each token (Issue 3).

    Yields: str tokens one at a time
    """
    generation_id = str(uuid.uuid4())
    cancel_event = _register_cancel_event(generation_id)

    model = _get_model(_active_model_name)

    formatted = (
        f"<|system|>\nYou are a helpful personal finance assistant.\n</s>\n"
        f"<|user|>\n{prompt}\n</s>\n"
        f"<|assistant|>\n"
    )

    # Blocks here if another request holds the lock (Issue 2)
    with _inference_lock:
        _mark_busy(generation_id)
        try:
            stream = model(
                formatted,
                max_tokens=max_tokens or _active_max_tokens,
                stream=True,
                stop=["</s>", "<|user|>", "<|system|>"],
                echo=False,
                temperature=0.7,
            )

            for chunk in stream:
                # Check before processing chunk (Issue 3)
                if cancel_event.is_set():
                    break

                token = chunk["choices"][0]["text"]
                if token:
                    yield token

                # Check after yield — stop() may have been called while
                # this thread was suspended waiting for the Rust channel
                if cancel_event.is_set():
                    break

        finally:
            # Always runs: cleans up even if an exception is raised mid-stream
            _mark_idle()
            _unregister_cancel_event(generation_id)