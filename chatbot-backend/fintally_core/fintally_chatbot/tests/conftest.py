"""
fintally_core/fintally_chatbot/tests/conftest.py
─────────────────────────────────────────────────────────────────────────────
Ensures chatbot-backend/ is discoverable and importable during pytest 
collection loops across isolated nested directories.

Why:
    Both python_llama.py and fintally_embedder.py reside in the chatbot-backend/
    root directory. Pytest operates deeply nested inside fintally_core/. 
    This config walks upward dynamically to find and append the backend root
    to sys.path, enabling seamless real integration testing.
"""

from pathlib import Path
import sys


def _find_backend_root(start: Path) -> Path:
    """
    Walks upward through directory levels until the core root is identified.
    Returns the resolved parent Path.
    """
    current = start.resolve()

    while current != current.parent:
        # Check for core file indicators to establish workspace identity
        if (current / "python_llama.py").exists() and (current / "fintally_embedder.py").exists():
            return current
        current = current.parent

    raise RuntimeError(
        "Could not locate chatbot-backend root directory containing production integration scripts."
    )


# Locate and append backend roots cleanly
_THIS_FILE = Path(__file__)
_BACKEND_ROOT = _find_backend_root(_THIS_FILE.parent)

if str(_BACKEND_ROOT) not in sys.path:
    sys.path.insert(0, str(_BACKEND_ROOT))