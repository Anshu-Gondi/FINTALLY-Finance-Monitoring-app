"""
rag_service.py

Torch-free lightweight RAG system.

Uses:
- transformers
- onnxruntime
- faiss-cpu
- numpy

Good for:
- low RAM systems
- CPU-only inference
- lightweight financial RAG

Install:

pip install transformers tokenizers onnxruntime faiss-cpu numpy
"""

import os
import json
import faiss
import numpy as np

from typing import List, Dict
from transformers import AutoTokenizer
import onnxruntime as ort


# =========================================================
# CONFIG
# =========================================================

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

INDEX_PATH = os.path.join(BASE_DIR, "faiss_index.bin")
CHUNKS_PATH = os.path.join(BASE_DIR, "chunks.json")

MODEL_NAME = "sentence-transformers/all-MiniLM-L6-v2"

TOP_K = 5


# =========================================================
# TOKENIZER
# =========================================================

print("Loading tokenizer...")

tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)

print("Tokenizer loaded.")


# =========================================================
# ONNX MODEL
# =========================================================

"""
IMPORTANT:

You must export/download ONNX model manually.

Recommended:
onnx/model.onnx

Folder structure:

rag/
│
├── rag_service.py
├── onnx/
│   └── model.onnx
"""

ONNX_MODEL_PATH = os.path.join(
    BASE_DIR,
    "onnx",
    "model.onnx"
)

session = ort.InferenceSession(
    ONNX_MODEL_PATH,
    providers=["CPUExecutionProvider"]
)

print("ONNX embedding model loaded.")


# =========================================================
# GLOBALS
# =========================================================

index = None
chunks: List[Dict] = []


# =========================================================
# MEAN POOLING
# =========================================================

def mean_pooling(token_embeddings, attention_mask):

    input_mask_expanded = np.expand_dims(
        attention_mask,
        axis=-1
    )

    sum_embeddings = np.sum(
        token_embeddings * input_mask_expanded,
        axis=1
    )

    sum_mask = np.clip(
        input_mask_expanded.sum(axis=1),
        a_min=1e-9,
        a_max=None
    )

    return sum_embeddings / sum_mask


# =========================================================
# NORMALIZATION
# =========================================================

def normalize(vectors):

    norms = np.linalg.norm(
        vectors,
        axis=1,
        keepdims=True
    )

    return vectors / np.clip(
        norms,
        a_min=1e-12,
        a_max=None
    )


# =========================================================
# EMBEDDING FUNCTION
# =========================================================

def embed_texts(texts: List[str]) -> np.ndarray:

    encoded = tokenizer(
        texts,
        padding=True,
        truncation=True,
        max_length=256,
        return_tensors="np"
    )

    ort_inputs = {
        "input_ids": encoded["input_ids"],
        "attention_mask": encoded["attention_mask"]
    }

    outputs = session.run(None, ort_inputs)

    token_embeddings = outputs[0]

    embeddings = mean_pooling(
        token_embeddings,
        encoded["attention_mask"]
    )

    embeddings = normalize(embeddings)

    return embeddings.astype("float32")


# =========================================================
# CHUNKING
# =========================================================

def chunk_text(
    text: str,
    chunk_size: int = 500,
    overlap: int = 100
) -> List[str]:

    text = text.strip()

    if not text:
        return []

    chunks = []

    start = 0

    while start < len(text):

        end = start + chunk_size

        chunks.append(text[start:end])

        start += chunk_size - overlap

    return chunks


# =========================================================
# BUILD INDEX
# =========================================================

def build_index(documents: List[Dict]):

    global index
    global chunks

    chunks = []

    texts = []

    for doc in documents:

        doc_chunks = chunk_text(doc["text"])

        for chunk in doc_chunks:

            chunks.append({
                "id": doc["id"],
                "text": chunk
            })

            texts.append(chunk)

    print(f"Embedding {len(texts)} chunks...")

    embeddings = embed_texts(texts)

    dimension = embeddings.shape[1]

    index = faiss.IndexFlatIP(dimension)

    index.add(embeddings)

    faiss.write_index(index, INDEX_PATH)

    with open(CHUNKS_PATH, "w", encoding="utf-8") as f:
        json.dump(chunks, f, ensure_ascii=False, indent=2)

    print("FAISS index saved.")


# =========================================================
# LOAD INDEX
# =========================================================

def load_index():

    global index
    global chunks

    if not os.path.exists(INDEX_PATH):
        raise FileNotFoundError("Missing FAISS index.")

    if not os.path.exists(CHUNKS_PATH):
        raise FileNotFoundError("Missing chunks.json.")

    index = faiss.read_index(INDEX_PATH)

    with open(CHUNKS_PATH, "r", encoding="utf-8") as f:
        chunks = json.load(f)

    print("FAISS index loaded.")


# =========================================================
# SEARCH
# =========================================================

def search(
    query: str,
    top_k: int = TOP_K
) -> List[Dict]:

    global index
    global chunks

    if index is None:
        load_index()

    query_embedding = embed_texts([query])

    scores, indices = index.search(
        query_embedding,
        top_k
    )

    results = []

    for score, idx in zip(scores[0], indices[0]):

        if idx < 0:
            continue

        chunk = chunks[idx]

        results.append({
            "score": float(score),
            "id": chunk["id"],
            "text": chunk["text"]
        })

    return results


# =========================================================
# CONTEXT BUILDER
# =========================================================

def build_context(
    query: str,
    top_k: int = TOP_K
) -> str:

    results = search(query, top_k)

    context_parts = []

    for i, item in enumerate(results, start=1):

        context_parts.append(
            f"[Context {i}]\n{item['text']}"
        )

    return "\n\n".join(context_parts)


# =========================================================
# EXAMPLE DOCS
# =========================================================

EXAMPLE_DOCS = [
    {
        "id": "single_parent_budget",
        "text": """
        Single parent budgeting profile.

        Housing:
        30 to 40 percent.

        Childcare:
        10 to 20 percent.

        Emergency fund:
        High priority.

        Healthcare spending elevated.
        """
    },

    {
        "id": "young_growth_investing",
        "text": """
        Young professionals with high risk tolerance
        should prioritize equity investments.

        Suggested:
        80 percent equity
        20 percent debt.
        """
    },

    {
        "id": "emi_guidelines",
        "text": """
        EMI should remain below
        40 percent of monthly income.

        High EMI ratios increase risk.
        """
    }
]


# =========================================================
# MAIN
# =========================================================

if __name__ == "__main__":

    if not os.path.exists(INDEX_PATH):

        print("Building FAISS index...")

        build_index(EXAMPLE_DOCS)

    else:

        load_index()

    while True:

        query = input("\nQuery: ")

        if query.lower() == "exit":
            break

        results = search(query)

        print("\nRESULTS:\n")

        for r in results:

            print("=" * 50)

            print(f"Score: {r['score']:.4f}")

            print(f"ID: {r['id']}")

            print(r["text"])

        print("\nCONTEXT:\n")

        print(build_context(query))