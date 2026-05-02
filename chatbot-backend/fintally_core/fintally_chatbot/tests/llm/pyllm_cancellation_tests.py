from fintally_chatbot.llm import create_llm

def test_stream_cancel():
    llm = create_llm("test-model", 128)
    stream = llm.stream("hello")

    first = next(stream)
    assert first is not None

    stream.cancel()

    # After cancel, iterator must end
    try:
        next(stream)
        assert False, "Stream should stop after cancel"
    except StopIteration:
        pass
