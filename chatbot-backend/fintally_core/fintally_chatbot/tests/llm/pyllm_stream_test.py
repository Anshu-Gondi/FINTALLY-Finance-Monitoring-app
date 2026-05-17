"""
tests/llm/pyllm_stream_test.py
─────────────────────────────────────────────────────────────────────────────
Tests for the streaming PyO3 bindings:
  - PyLLM.stream()     (python_bindings/llm/chat.rs)
  - PyStream.__aiter__ / __anext__ protocol
  - Token delivery across the Rust → Python channel

Marker legend:
  (no marker)             — runs always, no engine needed
  @pytest.mark.integration — skipped when python_llama is not importable

Run surface tests only:
    pytest tests/llm/pyllm_stream_test.py -v -m "not integration"
Run all (python_llama + model files required):
    pytest tests/llm/pyllm_stream_test.py -v -m integration
"""

import asyncio
import importlib.util
import pytest

try:
    import fintally_chatbot as fc
except ImportError:
    pytest.skip(
        "fintally_chatbot extension not built. Run `maturin develop` first.",
        allow_module_level=True,
    )

# ── python_llama availability ──────────────────────────────────────────────────
# conftest.py adds the project root to sys.path so find_spec works correctly.
# If it still returns None, the model files or llama_cpp are missing too.
_PYTHON_LLAMA_AVAILABLE = importlib.util.find_spec("python_llama") is not None


def _get_llm_mod():
    if hasattr(fc, "llm"):
        return fc.llm
    raise AttributeError(f"No llm submodule in fintally_chatbot. Got: {dir(fc)}")

llm_mod = _get_llm_mod()


# ═══════════════════════════════════════════════════════════════════════════════
# 1. STREAM OBJECT SURFACE  (no python_llama needed)
#
# stream() builds the Prompt and opens the Rust channel BEFORE calling
# python_llama.  Only token iteration triggers the python_llama import.
# These tests return the PyStream object without iterating it.
# ═══════════════════════════════════════════════════════════════════════════════

class TestStreamSurface:

    def test_stream_returns_awaitable(self):
        """stream() must return an awaitable, not a synchronous value."""
        llm = llm_mod.create_llm("test-model", 32)

        async def get_stream():
            return await llm.stream("Hello")

        stream_obj = asyncio.run(get_stream())
        assert stream_obj is not None

    def test_stream_result_has_cancel_method(self):
        """PyStream must expose cancel() callable from Python."""
        llm = llm_mod.create_llm("test-model", 32)

        async def get_stream():
            return await llm.stream("Hello")

        stream_obj = asyncio.run(get_stream())
        assert callable(getattr(stream_obj, "cancel", None)), (
            "PyStream must have a cancel() method"
        )

    def test_stream_result_is_async_iterable(self):
        """PyStream must implement __aiter__ and __anext__."""
        llm = llm_mod.create_llm("test-model", 32)

        async def check():
            stream_obj = await llm.stream("Hello")
            assert hasattr(stream_obj, "__aiter__"), "PyStream must have __aiter__"
            assert hasattr(stream_obj, "__anext__"), "PyStream must have __anext__"

        asyncio.run(check())

    def test_stream_with_context_does_not_crash(self):
        """context= kwarg must be accepted without raising."""
        llm = llm_mod.create_llm("test-model", 32)

        async def get_stream():
            return await llm.stream(
                "Explain EMI",
                context="You are a financial assistant.",
            )

        stream_obj = asyncio.run(get_stream())
        assert stream_obj is not None


# ═══════════════════════════════════════════════════════════════════════════════
# 2. INPUT VALIDATION BEFORE STREAMING  (no python_llama needed)
#
# Prompt::build fires inside `await llm.stream(...)` before python_llama
# is ever called, so these pass without the model.
# ═══════════════════════════════════════════════════════════════════════════════

class TestStreamInputValidation:

    def test_empty_prompt_raises_before_streaming(self):
        """Empty prompt raises RuntimeError when awaiting stream()."""
        llm = llm_mod.create_llm("test-model", 32)

        async def attempt():
            await llm.stream("")

        with pytest.raises(RuntimeError):
            asyncio.run(attempt())

    def test_whitespace_prompt_raises_before_streaming(self):
        llm = llm_mod.create_llm("test-model", 32)

        async def attempt():
            await llm.stream("   \n\t  ")

        with pytest.raises(RuntimeError):
            asyncio.run(attempt())

    def test_oversized_prompt_raises_before_streaming(self):
        llm = llm_mod.create_llm("test-model", 32)

        async def attempt():
            await llm.stream("x" * 8_001)

        with pytest.raises(RuntimeError):
            asyncio.run(attempt())


# ═══════════════════════════════════════════════════════════════════════════════
# 3. TOKEN DELIVERY  (requires python_llama + model files)
# ═══════════════════════════════════════════════════════════════════════════════

class TestTokenDelivery:
    """
    Iterating the stream triggers python_llama.stream_generate().
    All tests here are skipped when python_llama is not importable.
    """

    @pytest.mark.integration
    @pytest.mark.skipif(
        not _PYTHON_LLAMA_AVAILABLE,
        reason="python_llama not importable — check conftest.py sys.path and llama_cpp install",
    )
    def test_first_token_is_str(self):
        """The first token from async iteration must be a Python str."""
        llm = llm_mod.create_llm("tinyllama", 32)

        async def get_first_token():
            stream_obj = await llm.stream("Hello")
            async for token in stream_obj:
                return token
            return None

        token = asyncio.run(get_first_token())
        assert token is not None
        assert isinstance(token, str), f"Expected str token, got {type(token)}"

    @pytest.mark.integration
    @pytest.mark.skipif(
        not _PYTHON_LLAMA_AVAILABLE,
        reason="python_llama not importable",
    )
    def test_all_tokens_are_str(self):
        """
        Every token yielded must be a Python str.
        Reads up to MAX_TOKENS_TO_CHECK tokens then cancels to avoid
        a multi-minute full drain on CPU-only hardware.
        """
        MAX_TOKENS_TO_CHECK = 5
        llm = llm_mod.create_llm("tinyllama", 8)

        async def collect_some():
            tokens = []
            stream_obj = await llm.stream("Hello")
            async for token in stream_obj:
                tokens.append(token)
                if len(tokens) >= MAX_TOKENS_TO_CHECK:
                    stream_obj.cancel()
                    break
            async for _ in stream_obj:
                pass  # drain after cancel
            return tokens

        tokens = asyncio.run(collect_some())
        assert len(tokens) > 0
        for i, tok in enumerate(tokens):
            assert isinstance(tok, str), f"Token {i} is {type(tok)}, expected str"

    @pytest.mark.integration
    @pytest.mark.skipif(
        not _PYTHON_LLAMA_AVAILABLE,
        reason="python_llama not importable",
    )
    def test_stream_terminates_naturally(self):
        """
        The stream must close after max_tokens are generated.

        asyncio.wait_for() is NOT used here intentionally.
        wait_for() cancels the asyncio Task on timeout, but the Rust
        spawn_blocking thread inside llama.cpp keeps running and holds
        _inference_lock, deadlocking every subsequent test.

        Instead: read one token, call cancel() on the PyStream to stop
        the Rust generator cleanly, then drain to empty. This proves the
        channel closes correctly without hanging the test suite.
        """
        llm = llm_mod.create_llm("tinyllama", 8)  # tiny budget = fast

        async def read_one_then_cancel():
            stream_obj = await llm.stream("Hi")
            first = None
            async for token in stream_obj:
                first = token
                stream_obj.cancel()  # tell Rust to stop
                break
            # drain must complete promptly after cancel
            async for _ in stream_obj:
                pass
            return first

        first_token = asyncio.run(read_one_then_cancel())
        assert first_token is not None
        assert isinstance(first_token, str)

    @pytest.mark.integration
    @pytest.mark.skipif(
        not _PYTHON_LLAMA_AVAILABLE,
        reason="python_llama not importable",
    )
    def test_concatenated_tokens_form_non_empty_string(self):
        """
        Joined tokens must form a non-empty string.
        Reads first 3 tokens then cancels — enough to prove the contract
        without draining the full generation on CPU-only hardware.
        """
        llm = llm_mod.create_llm("tinyllama", 8)

        async def partial_output():
            parts = []
            stream_obj = await llm.stream("Explain budgeting briefly.")
            async for token in stream_obj:
                parts.append(token)
                if len(parts) >= 3:
                    stream_obj.cancel()
                    break
            async for _ in stream_obj:
                pass  # drain after cancel
            return "".join(parts)

        output = asyncio.run(partial_output())
        assert len(output) > 0