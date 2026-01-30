from fintally_chatbot.llm import create_llm

def test_generate_basic():
    llm = create_llm("test-model", 128)
    out = llm.generate("hello")

    assert isinstance(out, str)
    assert len(out) > 0

def test_embed_basic():
    llm = create_llm("test-model", 128)
    emb = llm.embed("hello")

    assert isinstance(emb, list)
    assert all(isinstance(x, float) for x in emb)