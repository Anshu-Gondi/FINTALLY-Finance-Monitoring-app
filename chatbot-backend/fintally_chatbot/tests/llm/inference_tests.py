import pytest
from concurrent.futures import ThreadPoolExecutor, TimeoutError
import _native  # Rust extension

# ─── Rust LLM objects ─────────────────────────────
PyLLM = _native.llm.PyLLM
create_llm = _native.llm.create_llm

# ─── Config ───────────────────────────────
timeout = 5  # seconds per test
executor = ThreadPoolExecutor(max_workers=2)  # safe for dual-core CPU

# ─── Fixture ─────────────────────────────
@pytest.fixture
def real_llm():
    return create_llm("test-model", max_tokens=16)


# ─── Helper to run blocking Rust calls safely ─────
def run_with_timeout(func, *args, **kwargs):
    """
    Executes a blocking Rust function in the dedicated executor.
    Fails the test if it exceeds the timeout.
    """
    future = executor.submit(func, *args, **kwargs)
    try:
        return future.result(timeout=timeout)
    except TimeoutError:
        pytest.fail(f"{func.__name__} exceeded timeout of {timeout} seconds")


# ─── Tests ──────────────────────────────────
def test_real_generate(real_llm):
    output = run_with_timeout(real_llm.generate, "Hello world")
    assert isinstance(output, str)
    assert len(output) > 0
    print("generate output:", output)


def test_real_embed(real_llm):
    emb = run_with_timeout(real_llm.embed, "Some text")
    assert isinstance(emb, list)
    assert all(isinstance(x, float) for x in emb)
    assert len(emb) > 0
    print("embed output:", emb)


def test_real_stream_collect(real_llm):
    stream = real_llm.stream("Stream test")

    def collect_all():
        # Collect all tokens from Rust blocking iterator
        collected = []
        for tok in stream:
            collected.append(tok)
        return collected

    collected = run_with_timeout(collect_all)
    assert len(collected) > 0
    print("stream collected:", collected)


def test_real_stream_cancel(real_llm):
    stream = real_llm.stream("Cancel test")

    # Read first token
    first = run_with_timeout(lambda: next(stream))
    assert isinstance(first, str)

    # Cancel the stream immediately
    run_with_timeout(stream.cancel)

    # Remaining tokens should raise StopIteration
    with pytest.raises(StopIteration):
        run_with_timeout(lambda: next(stream))
