# app/services/rag_engine.py
from llama_cpp import Llama
from pathlib import Path
from .vectorstore import search_similar_chunks
import re
import os

# Always base it on the script file location, not CWD
BASE_DIR = Path(__file__).resolve().parent.parent  # This gives 'app' folder
LLM_PATH = BASE_DIR.parent / "models" / "phi-2.Q4_K_M.gguf"

if not LLM_PATH.exists():
    raise FileNotFoundError(f"Model not found at {LLM_PATH}")

# Lowered contexts & batch sizes for memory-limited machines
llm = Llama(
    model_path=str(LLM_PATH),
    n_ctx=512,        # lowered from 1024 to save KV cache memory
    n_threads=2,
    n_batch=4,
    temperature=0.25,
    top_k=40,
    top_p=0.9,
    repeat_penalty=1.1,
    verbose=False
)

def extract_word_limit(query: str) -> int:
    match = re.search(r'(\d+)\s*words?', query.lower())
    if match:
        return min(int(match.group(1)), 300)
    return 100

def detect_tone_or_mode(query: str) -> str:
    ql = query.lower()
    if "12-year-old" in ql or "child" in ql:
        return "Explain like you're talking to a child."
    if "school" in ql:
        return "Write this as a school-level paragraph."
    if "professor" in ql:
        return "Answer like a finance professor using clear academic tone."
    if "friend" in ql:
        return "Explain casually like you're a helpful friend."
    if "advisor" in ql:
        return "Act like a financial advisor giving practical advice."
    if "blog" in ql:
        return "Write this like a blog post with engaging storytelling."
    return "Answer clearly and concisely for a general audience."

def ask_llm(prompt: str, max_tokens: int = 512) -> str:
    # Guard around llama call to avoid crashes bubbling up
    try:
        output = llm(prompt, max_tokens=max_tokens, stop=["</s>"])
        return output["choices"][0]["text"].strip()
    except Exception as e:
        print("[ERROR] LLM inference failed:", e)
        return "Sorry — the model failed to generate an answer right now."

def ask_with_context(query: str) -> str:
    print("[INFO] Searching for similar chunks...")
    context_chunks = search_similar_chunks(query, top_k=3)
    print(f"[INFO] Found {len(context_chunks)} context chunks.")

    word_limit = extract_word_limit(query)
    tone_instruction = detect_tone_or_mode(query)

    if not context_chunks:
        # No context — call model with a safe fallback prompt
        fallback = (
            f"{tone_instruction}\nYou are a finance assistant. The user asked: {query}\n"
            "You don't have access to documents right now. Answer concisely and clearly."
        )
        return ask_llm(fallback, max_tokens=min(200, word_limit * 2))

    # compress context (shorten each chunk)
    def compress(c):
        words = c.split()
        if len(words) > 60:
            return " ".join(words[:60]) + "..."
        return c

    compressed = [compress(c) for c in context_chunks]
    context_text = "\n\n".join(compressed)

    prompt = f"""
Use the following context to answer the question.
{tone_instruction}
Limit the answer to approximately {word_limit} words.

Context:
{context_text}

Question: {query}
Answer:"""

    print("[DEBUG] Prompt (head):", prompt[:1200].replace("\n", " "))
    return ask_llm(prompt, max_tokens=word_limit * 2)
