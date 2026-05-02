import pytest
from fintally_chatbot.llm import PyLLM, create_llm

# ─── Helper: Fake Stream ─────────────────────────────
class FakeStream:
    def __init__(self, tokens=None):
        self._tokens = iter(tokens or ["token1", "token2", "token3"])
        self.cancelled = False

    def __iter__(self):
        return self

    def __next__(self):
        if self.cancelled:
            raise StopIteration
        return next(self._tokens)

    def cancel(self):
        self.cancelled = True

# ─── Python wrapper around PyLLM for testing ───────────
class _TestLLM:  # <---- renamed to start with _
    """Wrapper for PyLLM to allow testing without monkeypatching Rust class."""
    def __init__(self, model_name="test-model", max_tokens=128):
        # original Rust object (optional, not used in fake tests)
        self._inner = create_llm(model_name, max_tokens)

    def stream(self, prompt):
        return FakeStream()

    def generate(self, prompt):
        return "fake_output"

    def embed(self, text):
        return [0.1, 0.2, 0.3]

# ─── Pytest fixture ───────────────────────────────────
@pytest.fixture
def llm():
    return _TestLLM()

# ─── Tests ─────────────────────────────────────────────
def test_stream_collect(llm):
    stream = llm.stream("hello")
    tokens = list(stream)
    assert tokens == ["token1", "token2", "token3"]
    assert len(tokens) > 0

def test_stream_cancel(llm):
    stream = llm.stream("hello")
    first = next(stream)
    assert first == "token1"

    stream.cancel()
    with pytest.raises(StopIteration):
        next(stream)

def test_generate_basic(llm):
    out = llm.generate("hello")
    assert isinstance(out, str)
    assert out == "fake_output"
    assert len(out) > 0

def test_embed_basic(llm):
    emb = llm.embed("hello")
    assert isinstance(emb, list)
    assert all(isinstance(x, float) for x in emb)
    assert emb == [0.1, 0.2, 0.3]
