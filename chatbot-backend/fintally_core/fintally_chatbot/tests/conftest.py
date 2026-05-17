"""
tests/conftest.py
─────────────────────────────────────────────────────────────────────────────
Ensures chatbot-backend/ is importable during pytest collection.

Why:
    python_llama.py lives at:

        chatbot-backend/python_llama.py

    but pytest runs from:

        chatbot-backend/fintally_core/fintally_chatbot/

    so the backend root is not automatically on sys.path.

    Rust's PythonLlamaEngine ultimately does:

        py.import("python_llama")

    which fails unless chatbot-backend/ is importable.

This conftest walks upward from this file until it finds
python_llama.py, then inserts that directory into sys.path.

This is more robust than hardcoding "../../../".
"""

from pathlib import Path
import sys


def _find_backend_root(start: Path) -> Path:
    """
    Walk upward until python_llama.py is found.

    Raises:
        RuntimeError if the file cannot be found.
    """
    current = start.resolve()

    while current != current.parent:
        candidate = current / "python_llama.py"

        if candidate.exists():
            return current

        current = current.parent

    raise RuntimeError(
        "Could not locate chatbot-backend root containing python_llama.py"
    )


_THIS_FILE = Path(__file__)

_BACKEND_ROOT = _find_backend_root(_THIS_FILE.parent)

if str(_BACKEND_ROOT) not in sys.path:
    sys.path.insert(0, str(_BACKEND_ROOT))