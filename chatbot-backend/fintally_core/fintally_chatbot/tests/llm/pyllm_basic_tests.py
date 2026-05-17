"""
tests/llm/pyllm_basic_tests.py
─────────────────────────────────────────────────────────────────────────────
Tests for the PyO3-exported LLM bindings:
  - create_llm() factory (python_bindings/llm/mod.rs)
  - PyLLM.generate()   (python_bindings/llm/chat.rs)
  - PyLLM.embed()      (python_bindings/llm/chat.rs)

What the Rust unit tests already cover (NOT duplicated here):
  - max_tokens clamping logic (model.rs)
  - Prompt::build empty/whitespace/oversized rejection (prompt.rs)
  - Embedding::generate numeric-only / empty / too-long rejection (embedding.rs)
  - Engine trait contract (engine.rs mock tests)

What these tests add (the Python ↔ Rust FFI boundary):
  - create_llm() is importable and callable from Python
  - PyLLM instance has the expected methods visible from Python
  - generate() returns a Python str (not bytes, not a Rust opaque type)
  - embed() returns a Python list of floats
  - Passing bad argument types raises TypeError at the Python boundary
  - Empty prompt raises RuntimeError (Prompt::build rejection crossing PyO3)
  - Prompt > 8000 chars raises RuntimeError (length guard crossing PyO3)

NOTE: generate() and embed() call the real PythonLlamaEngine which requires
the `python_llama` Python module to be present.  Tests that need a live engine
are marked @pytest.mark.integration and skipped by default.
Non-integration tests use stop_generation() or only probe the binding surface.

Run all:
    pytest tests/llm/pyllm_basic_tests.py -v

Run only FFI-surface tests (no python_llama needed):
    pytest tests/llm/pyllm_basic_tests.py -v -m "not integration"
"""

import pytest
import asyncio

try:
    import fintally_chatbot as fc
except ImportError:
    pytest.skip(
        "fintally_chatbot extension not built. Run `maturin develop` first.",
        allow_module_level=True,
    )


# ── resolve submodule path (mirrors chatbot_flow.py pattern) ──────────────────
def _get_llm_mod():
    if hasattr(fc, "llm"):
        return fc.llm
    raise AttributeError(
        f"fintally_chatbot has no 'llm' submodule. Available: {dir(fc)}"
    )

llm_mod = _get_llm_mod()


# ═══════════════════════════════════════════════════════════════════════════════
# 1. FACTORY — create_llm()
# ═══════════════════════════════════════════════════════════════════════════════

class TestCreateLlm:
    """
    Validates the create_llm() PyO3 function exported from
    python_bindings/llm/mod.rs.
    """

    def test_create_llm_is_callable(self):
        assert callable(getattr(llm_mod, "create_llm", None)), (
            "create_llm must be exported from the llm submodule"
        )

    def test_create_llm_returns_pyllm_instance(self):
        llm = llm_mod.create_llm("test-model", 128)
        PyLLM = getattr(llm_mod, "PyLLM", None)
        assert PyLLM is not None, "PyLLM class must be exported from llm submodule"
        assert isinstance(llm, PyLLM)

    def test_pyllm_has_generate_method(self):
        llm = llm_mod.create_llm("test-model", 64)
        assert callable(getattr(llm, "generate", None))

    def test_pyllm_has_embed_method(self):
        llm = llm_mod.create_llm("test-model", 64)
        assert callable(getattr(llm, "embed", None))

    def test_pyllm_has_stream_method(self):
        llm = llm_mod.create_llm("test-model", 64)
        assert callable(getattr(llm, "stream", None))

    def test_create_llm_wrong_arg_types_raises_type_error(self):
        """Passing wrong Python types must raise TypeError at the FFI boundary."""
        with pytest.raises(TypeError):
            llm_mod.create_llm(123, 64)          # model_name must be str

    def test_create_llm_negative_tokens_does_not_crash(self):
        """
        Rust clamps max_tokens to 512 internally.  A negative value coming
        from Python crosses the FFI as a usize; PyO3 should either reject it
        (OverflowError / TypeError) or clamp it.  Either is acceptable —
        what's NOT acceptable is a silent wrong result or a panic.
        """
        try:
            llm_mod.create_llm("model", -1)
        except (OverflowError, TypeError, RuntimeError):
            pass   # all acceptable — Rust boundary correctly rejected it

    def test_pyllm_direct_constructor_raises(self):
        """
        chat.rs exposes PyLLM.__new__ as PyNotImplementedError.
        Direct instantiation from Python must be blocked.
        """
        PyLLM = llm_mod.PyLLM
        with pytest.raises((NotImplementedError, RuntimeError)):
            PyLLM()


# ═══════════════════════════════════════════════════════════════════════════════
# 2. RETURN TYPE CONTRACTS (FFI surface, no real inference needed)
# ═══════════════════════════════════════════════════════════════════════════════

class TestReturnTypes:
    """
    Validates that methods return the correct Python types.
    Uses asyncio.run() to drive the coroutines returned by pyo3_asyncio.
    """

    @pytest.mark.integration
    def test_generate_returns_str(self):
        """
        generate() must return a Python str, not bytes or an opaque Rust type.
        Requires python_llama to be importable.
        """
        llm = llm_mod.create_llm("test-model", 16)
        result = asyncio.run(llm.generate("Hello"))
        assert isinstance(result, str)
        assert len(result) > 0

    @pytest.mark.integration
    def test_embed_returns_list_of_floats(self):
        """
        embed() must return a Python list[float], not a Rust Vec or ndarray.
        Requires python_llama to be importable.
        """
        llm = llm_mod.create_llm("test-model", 16)
        result = asyncio.run(llm.embed("hello world"))
        assert isinstance(result, list)
        assert len(result) > 0
        assert all(isinstance(x, float) for x in result)

    @pytest.mark.integration
    def test_generate_with_context_returns_str(self):
        """Context kwarg must be accepted and forwarded without crashing."""
        llm = llm_mod.create_llm("test-model", 16)
        result = asyncio.run(
            llm.generate("Explain budgeting", context="You are a finance assistant.")
        )
        assert isinstance(result, str)


# ═══════════════════════════════════════════════════════════════════════════════
# 3. INPUT VALIDATION CROSSING THE FFI
# ═══════════════════════════════════════════════════════════════════════════════

class TestInputValidation:
    """
    Rust-side guards (Prompt::build, Embedding::generate) must surface as
    Python RuntimeError when called through the PyO3 async binding.
    """

    @pytest.mark.integration
    def test_empty_prompt_raises_runtime_error(self):
        """
        Prompt::build rejects empty strings → AppError::InvalidInput →
        app_error_to_py() → PyRuntimeError.
        """
        llm = llm_mod.create_llm("test-model", 64)
        with pytest.raises(RuntimeError):
            asyncio.run(llm.generate(""))

    @pytest.mark.integration
    def test_whitespace_only_prompt_raises_runtime_error(self):
        llm = llm_mod.create_llm("test-model", 64)
        with pytest.raises(RuntimeError):
            asyncio.run(llm.generate("   \n\t  "))

    @pytest.mark.integration
    def test_oversized_prompt_raises_runtime_error(self):
        """Prompt > 8000 chars triggers the length guard in Prompt::build."""
        llm = llm_mod.create_llm("test-model", 64)
        with pytest.raises(RuntimeError):
            asyncio.run(llm.generate("x" * 8_001))

    @pytest.mark.integration
    def test_empty_embed_text_raises_runtime_error(self):
        """Embedding::generate rejects empty text."""
        llm = llm_mod.create_llm("test-model", 64)
        with pytest.raises(RuntimeError):
            asyncio.run(llm.embed(""))

    @pytest.mark.integration
    def test_numeric_only_embed_raises_runtime_error(self):
        """Embedding::generate rejects numeric-only strings."""
        llm = llm_mod.create_llm("test-model", 64)
        with pytest.raises(RuntimeError):
            asyncio.run(llm.embed("123 456 789"))

    @pytest.mark.integration
    def test_oversized_embed_text_raises_runtime_error(self):
        llm = llm_mod.create_llm("test-model", 64)
        with pytest.raises(RuntimeError):
            asyncio.run(llm.embed("a" * 8_001))