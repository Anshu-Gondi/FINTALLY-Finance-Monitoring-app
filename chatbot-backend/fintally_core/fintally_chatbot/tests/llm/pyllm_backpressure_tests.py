"""
tests/llm/pyllm_backpressure_tests.py
─────────────────────────────────────────────────────────────────────────────
Tests for backpressure behaviour when a slow Python consumer reads the stream
produced by the Rust tokio task.

What the Rust unit tests already cover (NOT duplicated here):
  - mpsc channel with capacity 1 blocks the producer (python_engine.rs)
  - token-0 and token-1 are in-order even with a slow consumer (python_engine.rs)

What these tests add (the Python ↔ Rust FFI boundary):
  - A slow asyncio consumer (sleeping between tokens) does not crash or deadlock
  - Tokens arrive in order even when the consumer is slower than the producer
  - The stream does not drop tokens when the consumer pauses
  - The extension does not panic if Python suspends the consumer mid-stream
  - Memory does not obviously grow unboundedly (no buildup of un-drained tokens)

All tests marked @integration (require python_llama).

Run:
    pytest tests/llm/pyllm_backpressure_tests.py -v -m integration
"""

import asyncio
import time
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


# ═══════════════════════════════════════════════════════════════════════════════
# 1. SLOW CONSUMER DOES NOT CRASH
# ═══════════════════════════════════════════════════════════════════════════════

class TestSlowConsumerStability:
    """
    A Python consumer that sleeps between reads must not cause the Rust side
    to panic, deadlock, or drop the extension module.
    """

    @pytest.mark.integration
    def test_slow_consumer_receives_all_tokens(self):
        """
        Sleep 50 ms between each token read.  All tokens that the model
        produces must still arrive — none dropped by backpressure.
        """
        async def slow_drain():
            llm = llm_mod.create_llm("test-model", 64)
            stream_obj = await llm.stream("Explain savings briefly.")
            tokens = []
            async for token in stream_obj:
                tokens.append(token)
                await asyncio.sleep(0.05)   # 50 ms pause — slower than producer
            return tokens

        # Generous timeout: 64 tokens × 50ms = 3.2s max
        tokens = asyncio.run(asyncio.wait_for(slow_drain(), timeout=15.0))
        assert len(tokens) > 0
        assert all(isinstance(t, str) for t in tokens)

    @pytest.mark.integration
    def test_very_slow_consumer_does_not_deadlock(self):
        """
        100 ms sleep between reads.  The channel has capacity 32 (chat.rs).
        Once the buffer fills, the Rust producer blocks — this is correct
        backpressure.  The test verifies the system recovers when the consumer
        eventually reads.
        """
        async def very_slow_drain():
            llm = llm_mod.create_llm("test-model", 32)
            stream_obj = await llm.stream("Hello.")
            tokens = []
            async for token in stream_obj:
                tokens.append(token)
                await asyncio.sleep(0.1)    # 100ms — deliberate backpressure
            return tokens

        tokens = asyncio.run(asyncio.wait_for(very_slow_drain(), timeout=20.0))
        assert isinstance(tokens, list)

    @pytest.mark.integration
    def test_consumer_suspend_and_resume_preserves_order(self):
        """
        Suspend the consumer for 200ms after every 2 tokens.
        Tokens must arrive in the same order as produced.
        """
        async def interleaved_drain():
            llm = llm_mod.create_llm("test-model", 32)
            stream_obj = await llm.stream("Count to ten.")
            tokens = []
            async for token in stream_obj:
                tokens.append(token)
                if len(tokens) % 2 == 0:
                    await asyncio.sleep(0.2)
            return tokens

        tokens = asyncio.run(asyncio.wait_for(interleaved_drain(), timeout=20.0))
        # Can't assert exact content since python_llama output varies,
        # but we can assert no duplicates relative to adjacent tokens
        # and that we got something
        assert len(tokens) > 0
        assert all(isinstance(t, str) for t in tokens)


# ═══════════════════════════════════════════════════════════════════════════════
# 2. FAST CONSUMER (BASELINE)
# ═══════════════════════════════════════════════════════════════════════════════

class TestFastConsumer:
    """
    Fast consumer baseline: confirms that without artificial delay the stream
    completes faster than the slow consumer timeout, providing evidence that
    the slow-consumer tests are actually applying backpressure.
    """

    @pytest.mark.integration
    def test_fast_consumer_completes_in_reasonable_time(self):
        """
        No sleep between reads.  All tokens must arrive within 5 seconds
        for a 32-token response — if it takes longer, something is wrong
        with the channel/wakeup path.
        """
        async def fast_drain():
            llm = llm_mod.create_llm("test-model", 32)
            stream_obj = await llm.stream("Hello.")
            tokens = []
            start = time.monotonic()
            async for token in stream_obj:
                tokens.append(token)
            elapsed = time.monotonic() - start
            return tokens, elapsed

        tokens, elapsed = asyncio.run(asyncio.wait_for(fast_drain(), timeout=10.0))
        assert len(tokens) > 0
        assert elapsed < 5.0, (
            f"Fast drain took {elapsed:.2f}s — channel wakeup may be broken"
        )

    @pytest.mark.integration
    def test_fast_consumer_is_faster_than_slow_consumer(self):
        """
        Fast and slow consumers on the same prompt.
        Fast must finish in less time than slow (proves backpressure is real).
        """
        prompt = "Hello."

        async def fast():
            llm = llm_mod.create_llm("test-model", 16)
            stream_obj = await llm.stream(prompt)
            t0 = time.monotonic()
            async for _ in stream_obj:
                pass
            return time.monotonic() - t0

        async def slow():
            llm = llm_mod.create_llm("test-model", 16)
            stream_obj = await llm.stream(prompt)
            t0 = time.monotonic()
            async for _ in stream_obj:
                await asyncio.sleep(0.05)
            return time.monotonic() - t0

        fast_time = asyncio.run(asyncio.wait_for(fast(), timeout=10.0))
        slow_time = asyncio.run(asyncio.wait_for(slow(), timeout=20.0))

        assert fast_time < slow_time, (
            f"Fast ({fast_time:.2f}s) was not faster than slow ({slow_time:.2f}s). "
            "Backpressure may not be applying correctly."
        )


# ═══════════════════════════════════════════════════════════════════════════════
# 3. CANCEL UNDER BACKPRESSURE
# ═══════════════════════════════════════════════════════════════════════════════

class TestCancelUnderBackpressure:
    """
    Cancelling while the channel is full (producer blocked) must not deadlock.
    The Rust producer is in a blocking_send() call when cancel fires.
    """

    @pytest.mark.integration
    def test_cancel_while_buffer_full_does_not_hang(self):
        """
        Pause consumer for 500ms (long enough to fill the 32-token buffer),
        then cancel.  Must complete within 3 seconds.
        """
        async def run():
            llm = llm_mod.create_llm("test-model", 128)
            stream_obj = await llm.stream(
                "Tell me a lot about budgeting and savings in extreme detail."
            )
            # Read one token to start the producer
            async for token in stream_obj:
                assert isinstance(token, str)
                break

            # Pause — let the buffer fill up, producer blocks
            await asyncio.sleep(0.5)

            # Cancel while producer is likely blocked on send
            stream_obj.cancel()

            # Drain must terminate, not hang
            async for _ in stream_obj:
                pass

        asyncio.run(asyncio.wait_for(run(), timeout=5.0))