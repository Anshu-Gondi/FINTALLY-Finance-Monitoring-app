# app/services/vectorstore.py
import sqlite3
import numpy as np
from sentence_transformers import SentenceTransformer
from pathlib import Path
from typing import List, Tuple, Optional

BASE_DIR = Path(__file__).resolve().parent.parent
VECTOR_DB = BASE_DIR / "vectorstore" / "vectors.sqlite"

# Lazy-load embedder to save memory at startup
_embedder = None
def get_embedder():
    global _embedder
    if _embedder is None:
        print("[INFO] Loading embedding model (all-MiniLM-L6-v2)...")
        _embedder = SentenceTransformer("all-MiniLM-L6-v2")
    return _embedder

def cosine_sim_between_vec_and_matrix(q: np.ndarray, mat: np.ndarray) -> np.ndarray:
    # q shape (d,), mat shape (n, d)
    q_norm = q / (np.linalg.norm(q) + 1e-12)
    mat_norms = mat / (np.linalg.norm(mat, axis=1, keepdims=True) + 1e-12)
    return mat_norms.dot(q_norm)

def search_similar_chunks(query: str, top_k: int = 3) -> List[str]:
    """
    Searches the SQLite store and returns top_k most similar chunks.
    Implemented to avoid loading all embeddings into memory at once when possible.
    """
    embedder = get_embedder()
    q_emb = embedder.encode([query])[0].astype(np.float32)

    if not VECTOR_DB.exists():
        print("[WARN] Vector DB does not exist:", VECTOR_DB)
        return []

    conn = sqlite3.connect(VECTOR_DB)
    cursor = conn.cursor()

    # Check count quickly
    cursor.execute("SELECT COUNT(1) FROM vectors")
    total = cursor.fetchone()[0]
    if total == 0:
        conn.close()
        return []

    # We'll process in batches to limit memory usage
    batch_size = 1024  # tune this if you run into mem issues
    offset = 0
    top_candidates: List[Tuple[float, str]] = []

    while True:
        cursor.execute(
            "SELECT content, embedding FROM vectors LIMIT ? OFFSET ?",
            (batch_size, offset)
        )
        rows = cursor.fetchall()
        if not rows:
            break

        contents = []
        mats = []
        for content, emb_blob in rows:
            # emb_blob should be bytes of float32 array
            try:
                vec = np.frombuffer(emb_blob, dtype=np.float32)
                mats.append(vec)
                contents.append(content)
            except Exception as e:
                # skip malformed rows
                print("[WARN] Bad embedding row skipped:", e)
                continue

        if mats:
            mat = np.vstack(mats)  # shape (n, d)
            sims = cosine_sim_between_vec_and_matrix(q_emb, mat)  # shape (n,)
            # Collect local top_k
            for sim_score, cont in zip(sims.tolist(), contents):
                top_candidates.append((sim_score, cont))

            # Keep only global top_k to bound memory
            top_candidates.sort(key=lambda x: x[0], reverse=True)
            top_candidates = top_candidates[: max(top_k * 3, 50)]  # keep some headroom

        offset += batch_size

    conn.close()

    # Final sort and return top_k contents
    top_candidates.sort(key=lambda x: x[0], reverse=True)
    return [c for _, c in top_candidates[:top_k]]
