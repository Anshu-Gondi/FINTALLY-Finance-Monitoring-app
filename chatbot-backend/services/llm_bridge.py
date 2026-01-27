# services/llm_bridge.py

def llama_generate(prompt: str, max_tokens: int) -> str:
    return llama(
        prompt,
        max_tokens=max_tokens,
        temperature=0.7,
    )

def llama_embed(text: str) -> list[float]:
    return llama.embed(text)
