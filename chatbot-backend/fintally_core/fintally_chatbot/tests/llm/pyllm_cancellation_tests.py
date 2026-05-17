"""
tests/llm/pyllm_cancellation_tests.py
─────────────────────────────────────────────────────────────────────────────
Tests for cancellation of the PyStream object:
  - PyStream.cancel() callable from Python
  - cancel() propagates into the Rust CancellationToken
  - No tokens arrive after cancel() is called
  - Stream closes cleanly (no hang, no panic) after cancel

What the Rust unit tests already cover (NOT duplicated here):
  - CancellationToken.cancel() stops the producer task (python_engine.rs)
  - tokio::select! on cancel_child.cancelled() (python_engine.rs)
  - Stream emits token-0 then terminates after cancel (engine.rs)

What these tests add (the Python ↔ Rust FFI boundary):
  - cancel() is callable as a plain synchronous Python method on PyStream
  - After cancel() from Python, the `async for` loop terminates promptly
  - Calling cancel() before consuming any tokens doesn't hang or panic
  - Calling cancel() multiple times doesn't crash
  - Token count after cancel is strictly less than without cancel

All tests require python_llama and are marked @integration.

Run:
    pytest tests/llm/pyllm_cancellation_tests.py -v -m "not integration"
    pytest tests/llm/pyllm_cancellation_tests.py -v -m integration
"""

import asyncio
import pytest

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


# ── helpers ───────────────────────────────────────────────────────────────────

async def _open_stream(prompt: str = "Tell me about financial planning in detail",
                       max_tokens: int = 256):
    llm = llm_mod.create_llm("test-model", max_tokens)
    return await llm.stream(prompt)


# ═══════════════════════════════════════════════════════════════════════════════
# 1. CANCEL METHOD SURFACE
# ═══════════════════════════════════════════════════════════════════════════════

class TestCancelSurface:
    """
    cancel() is a synchronous Python method on the PyStream object.
    These tests validate it exists and is callable without requiring that
    python_llama is fully functional.
    """

    @pytest.mark.integration
    def test_cancel_method_exists_on_pystream(self):
        """PyStream must expose cancel() as a callable Python method."""
        async def check():
            stream_obj = await _open_stream()
            assert callable(getattr(stream_obj, "cancel", None)), (
                "PyStream must have a cancel() method"
            )
            stream_obj.cancel()  # call immediately so we don't block

        asyncio.run(check())

    @pytest.mark.integration
    def test_cancel_is_synchronous(self):
        """
        cancel() must be a plain synchronous call, not a coroutine.
        If it were async, callers would need `await cancel()` which is wrong.
        """
        async def check():
            stream_obj = await _open_stream()
            result = stream_obj.cancel()
            # A coroutine would be truthy here; None means sync void return
            assert result is None, (
                f"cancel() returned {result!r} — expected None (sync void)"
            )

        asyncio.run(check())

    @pytest.mark.integration
    def test_cancel_twice_does_not_panic(self):
        """Calling cancel() twice must not crash or raise."""
        async def check():
            stream_obj = await _open_stream()
            stream_obj.cancel()
            stream_obj.cancel()   # idempotent — CancellationToken supports this

        asyncio.run(check())  # must not raise


# ═══════════════════════════════════════════════════════════════════════════════
# 2. CANCEL STOPS TOKEN DELIVERY
# ═══════════════════════════════════════════════════════════════════════════════

class TestCancelStopsStream:
    """
    After cancel() is called from Python, the Rust producer task must stop
    sending tokens.  The `async for` loop must terminate without hanging.
    """

    @pytest.mark.integration
    def test_cancel_before_first_token_terminates_loop(self):
        """
        Cancelling before reading any tokens: the async for loop must
        complete (possibly with 0 tokens) and not block.
        """
        async def run():
            stream_obj = await _open_stream()
            stream_obj.cancel()   # cancel BEFORE iterating

            tokens = []
            async for token in stream_obj:
                tokens.append(token)
            return tokens

        # 5s timeout — if this hangs, cancellation is broken
        tokens = asyncio.run(asyncio.wait_for(run(), timeout=5.0))
        assert isinstance(tokens, list)

    @pytest.mark.integration
    def test_cancel_after_first_token_stops_further_delivery(self):
        """
        Read exactly one token, then cancel.  The loop must terminate
        and the total token count must be 1 (possibly 2 if one was buffered).
        """
        async def run():
            stream_obj = await _open_stream()
            tokens = []
            async for token in stream_obj:
                tokens.append(token)
                stream_obj.cancel()   # cancel after first token
                break                 # exit loop to stop consuming
            # drain — should terminate quickly after cancel
            async for token in stream_obj:
                tokens.append(token)
            return tokens

        tokens = asyncio.run(asyncio.wait_for(run(), timeout=5.0))
        # We got at least 1 (the one we read), and at most a small buffer's worth
        assert len(tokens) >= 1
        assert len(tokens) < 10, (
            f"Expected stream to stop promptly after cancel, got {len(tokens)} tokens"
        )

    @pytest.mark.integration
    def test_cancelled_stream_yields_fewer_tokens_than_uncancelled(self):
        """
        Control: full stream without cancel → N tokens.
        Treatment: cancel after 3 tokens → M tokens where M < N.
        This is the definitive proof that cancellation works end-to-end.
        """
        async def full_stream():
            llm = llm_mod.create_llm("test-model", 256)
            stream_obj = await llm.stream(
                "Tell me about financial planning in great detail."
            )
            tokens = []
            async for token in stream_obj:
                tokens.append(token)
            return tokens

        async def cancelled_stream():
            llm = llm_mod.create_llm("test-model", 256)
            stream_obj = await llm.stream(
                "Tell me about financial planning in great detail."
            )
            tokens = []
            async for token in stream_obj:
                tokens.append(token)
                if len(tokens) >= 3:
                    stream_obj.cancel()
                    break
            # small drain window
            try:
                async for token in asyncio.wait_for(
                    _drain(stream_obj), timeout=1.0
                ):
                    tokens.append(token)
            except asyncio.TimeoutError:
                pass
            return tokens

        async def _drain(stream_obj):
            async for token in stream_obj:
                yield token

        full_count = len(asyncio.run(asyncio.wait_for(full_stream(), timeout=15.0)))
        cancelled_count = len(asyncio.run(asyncio.wait_for(cancelled_stream(), timeout=10.0)))

        assert cancelled_count < full_count, (
            f"Cancelled stream yielded {cancelled_count} tokens, "
            f"uncancelled yielded {full_count}. Cancellation had no effect."
        )

    @pytest.mark.integration
    def test_stream_does_not_hang_after_cancel(self):
        """
        The most critical property: cancel() + drain must complete within
        a strict timeout.  A hang here means the channel is never closed.
        """
        async def run():
            stream_obj = await _open_stream()
            stream_obj.cancel()

            # Try to drain whatever is buffered
            async for _ in stream_obj:
                pass   # just drain

        # 3s is very generous for a cancelled stream
        asyncio.run(asyncio.wait_for(run(), timeout=3.0))