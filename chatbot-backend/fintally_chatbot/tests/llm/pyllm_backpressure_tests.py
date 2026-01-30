import time
from fintally_chatbot.llm import create_llm # type: ignore

def test_stream_backpressure():
    llm = create_llm("test-model", 128)
    stream = llm.stream("slow consumer test")

    tokens = []
    for tok in stream:
        tokens.append(tok)
        time.sleep(0.05)  # simulate slow consumer

    assert len(tokens) > 0
