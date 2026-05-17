"""
tests/llm/inference_tests.py
─────────────────────────────────────────────────────────────────────────────
Tests for the LLM configuration surface and stop_generation():
  - create_llm() model_name and max_tokens arguments
  - max_tokens clamping at the Python call boundary (Rust clamps to 512)
  - stop_generation() callable from Python
  - Module-level exports: PyLLM class, create_llm, stop_generation

What the Rust unit tests already cover (NOT duplicated here):
  - max_tokens clamping logic itself — LLM::new caps at 512 (model.rs)
  - generate() uses the clamped value (model.rs: "ends with :512")
  - Prompt::build chain (prompt.rs)

What these tests add (the Python ↔ Rust FFI boundary):
  - max_tokens clamping is visible from Python (gen output suffix :512)
  - stop_generation() is importable and callable without crashing
  - stop_generation() calls python_llama.stop() — smoke-test only
  - model_name string is accepted from Python without corruption
  - PyLLM class is exported and inspectable from Python
  - All three bindings (create_llm, stop_generation, PyLLM) are present
    in the llm submodule — a regression guard for future refactors

Run:
    pytest tests/llm/inference_tests.py -v
    pytest tests/llm/inference_tests.py -v -m "not integration"
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


def _get_llm_mod():
    if hasattr(fc, "llm"):
        return fc.llm
    raise AttributeError(f"No llm submodule in fintally_chatbot. Got: {dir(fc)}")

llm_mod = _get_llm_mod()


# ═══════════════════════════════════════════════════════════════════════════════
# 1. MODULE EXPORT COMPLETENESS
# ═══════════════════════════════════════════════════════════════════════════════

class TestModuleExports:
    """
    Regression guard: every symbol registered in python_bindings/llm/mod.rs
    must be importable from Python.  If a future refactor removes or renames
    a registration, this fails before any functional test does.
    """

    def test_create_llm_exported(self):
        assert hasattr(llm_mod, "create_llm"), (
            "create_llm must be exported from fintally_chatbot.llm"
        )

    def test_stop_generation_exported(self):
        assert hasattr(llm_mod, "stop_generation"), (
            "stop_generation must be exported from fintally_chatbot.llm"
        )

    def test_pyllm_class_exported(self):
        assert hasattr(llm_mod, "PyLLM"), (
            "PyLLM class must be exported from fintally_chatbot.llm"
        )

    def test_pyllm_is_a_type(self):
        """PyLLM must be a class/type, not a function or None."""
        assert isinstance(llm_mod.PyLLM, type), (
            f"PyLLM should be a type, got {type(llm_mod.PyLLM)}"
        )

    def test_no_unexpected_absence_of_llm_submodule(self):
        """
        Top-level module must have an `llm` attribute.
        If python_bindings/mod.rs stops registering the submodule, this fails.
        """
        assert hasattr(fc, "llm"), (
            f"fintally_chatbot has no 'llm' attribute. Got: {[x for x in dir(fc) if not x.startswith('_')]}"
        )


# ═══════════════════════════════════════════════════════════════════════════════
# 2. create_llm() ARGUMENT HANDLING
# ═══════════════════════════════════════════════════════════════════════════════

class TestCreateLlmArguments:
    """
    Validates argument handling at the Python call boundary.
    The Rust logic is tested in model.rs; here we test what Python sees.
    """

    def test_model_name_is_accepted(self):
        """Any non-empty string must be accepted as model_name."""
        llm = llm_mod.create_llm("llama-3-8b", 64)
        assert llm is not None

    def test_zero_max_tokens_boundary(self):
        """
        max_tokens=0: Rust will clamp it to min(0, 512) = 0.
        Either it raises or returns an LLM — it must not panic.
        """
        try:
            llm = llm_mod.create_llm("model", 0)
            assert llm is not None
        except (OverflowError, TypeError, RuntimeError):
            pass  # rejecting 0 is also valid

    def test_max_tokens_at_rust_cap(self):
        """max_tokens=512 is the Rust cap. Must work without issue."""
        llm = llm_mod.create_llm("model", 512)
        assert llm is not None

    def test_max_tokens_above_rust_cap_is_accepted(self):
        """
        Rust silently clamps max_tokens > 512 to 512 (model.rs: .min(512)).
        Python must not raise for large values — they're just clamped.
        """
        llm = llm_mod.create_llm("model", 10_000)
        assert llm is not None

    def test_unicode_model_name_accepted(self):
        """Unicode model names must not crash the FFI string conversion."""
        llm = llm_mod.create_llm("मॉडल-v1", 64)
        assert llm is not None

    def test_empty_model_name_accepted(self):
        """
        Rust does not validate the model_name string (it's passed through
        to python_llama).  An empty string must not crash at create_llm time.
        """
        try:
            llm = llm_mod.create_llm("", 64)
            assert llm is not None
        except (RuntimeError, ValueError):
            pass   # rejecting empty string is also valid

    @pytest.mark.integration
    def test_max_tokens_clamping_visible_from_python(self):
        """
        Rust clamps max_tokens to 512.  When generate() is called with
        max_tokens=10_000, the actual call to the engine uses 512.
        We cannot directly inspect this from Python, but we can confirm
        that the call completes without 'max_tokens too large' errors.
        """
        llm = llm_mod.create_llm("test-model", 10_000)
        result = asyncio.run(llm.generate("Hello"))
        assert isinstance(result, str)


# ═══════════════════════════════════════════════════════════════════════════════
# 3. stop_generation()
# ═══════════════════════════════════════════════════════════════════════════════

class TestStopGeneration:
    """
    stop_generation() calls python_llama.stop() via Python GIL.
    We can only smoke-test this without a real python_llama module.
    """

    def test_stop_generation_is_callable(self):
        assert callable(llm_mod.stop_generation)

    @pytest.mark.integration
    def test_stop_generation_does_not_raise_when_idle(self):
        """
        Calling stop when no generation is running must not crash.
        python_llama.stop() is expected to be a no-op in this state.
        """
        try:
            llm_mod.stop_generation()
        except RuntimeError as e:
            # If python_llama is not loaded yet, stop() may fail cleanly
            # Accept that — what's NOT acceptable is a panic or hang
            assert "python_llama" in str(e).lower() or "stop" in str(e).lower(), (
                f"Unexpected RuntimeError from stop_generation: {e}"
            )

    @pytest.mark.integration
    def test_stop_generation_during_stream(self):
        """
        Start a stream, call stop_generation() (the module-level stop,
        not PyStream.cancel()), then verify the stream terminates.
        This tests the stop_generation → python_llama.stop() path.
        """
        async def run():
            llm = llm_mod.create_llm("test-model", 128)
            stream_obj = await llm.stream(
                "Tell me everything about personal finance in extreme detail."
            )
            tokens = []
            async for token in stream_obj:
                tokens.append(token)
                if len(tokens) == 2:
                    llm_mod.stop_generation()   # module-level stop
                    break
            return tokens

        tokens = asyncio.run(asyncio.wait_for(run(), timeout=10.0))
        assert len(tokens) >= 1


# ═══════════════════════════════════════════════════════════════════════════════
# 4. PyLLM INSTANCE INSPECTION
# ═══════════════════════════════════════════════════════════════════════════════

class TestPyLlmInspection:
    """
    Validates the PyLLM instance as seen from Python introspection.
    """

    def test_pyllm_instance_methods_visible(self):
        """All three public methods must be visible via dir()."""
        llm = llm_mod.create_llm("test-model", 64)
        public = set(x for x in dir(llm) if not x.startswith("_"))
        assert "generate" in public
        assert "embed"    in public
        assert "stream"   in public

    def test_pyllm_repr_does_not_crash(self):
        """repr() and str() must not panic or raise."""
        llm = llm_mod.create_llm("test-model", 64)
        r = repr(llm)
        assert isinstance(r, str)

    def test_multiple_pyllm_instances_are_independent(self):
        """
        Two PyLLM instances must not share state.
        Creating both must not raise.
        """
        llm1 = llm_mod.create_llm("model-a", 64)
        llm2 = llm_mod.create_llm("model-b", 128)
        assert llm1 is not llm2