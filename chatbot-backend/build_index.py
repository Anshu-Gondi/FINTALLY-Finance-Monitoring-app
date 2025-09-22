# build_index.py
import os
import json
import hashlib
import sqlite3
from pathlib import Path
import fitz  # PyMuPDF
from sentence_transformers import SentenceTransformer
import numpy as np

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data" / "financial_books"
VECTOR_DB = BASE_DIR / "app" / "vectorstore" / "vectors.sqlite"
VECTOR_DB.parent.mkdir(exist_ok=True)

MANIFEST_PATH = BASE_DIR / "app" / "vectorstore" / "manifest.json"
embedder = SentenceTransformer("all-MiniLM-L6-v2")

CHUNK_SIZE = 500  # characters per chunk
CHUNK_OVERLAP = 50

def hash_file(path):
    return hashlib.md5(open(path, 'rb').read()).hexdigest()

def load_manifest():
    if MANIFEST_PATH.exists():
        with open(MANIFEST_PATH, "r") as f:
            return json.load(f)
    return {}

def save_manifest(manifest):
    with open(MANIFEST_PATH, "w") as f:
        json.dump(manifest, f)

def chunk_text(text, size=CHUNK_SIZE, overlap=CHUNK_OVERLAP):
    chunks = []
    start = 0
    while start < len(text):
        end = min(start + size, len(text))
        chunks.append(text[start:end])
        start += size - overlap
    return chunks

def extract_and_store(pdf_path, conn):
    doc = fitz.open(pdf_path)
    all_chunks = []
    for page in doc:
        text = page.get_text().strip()
        if len(text) > 100:
            chunks = chunk_text(text)
            all_chunks.extend(chunks)
    doc.close()

    if all_chunks:
        embeddings = embedder.encode(all_chunks, show_progress_bar=True)
        cursor = conn.cursor()
        for chunk, vector in zip(all_chunks, embeddings):
            cursor.execute(
                "INSERT INTO vectors (content, embedding) VALUES (?, ?)",
                (chunk, vector.tobytes())
            )
        conn.commit()
        print(f"✅ Added {len(all_chunks)} chunks from {pdf_path.name}")

def init_db():
    conn = sqlite3.connect(VECTOR_DB)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS vectors (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            content TEXT,
            embedding BLOB
        )
    """)
    return conn

if __name__ == "__main__":
    manifest = load_manifest()
    conn = init_db()

    for file in os.listdir(DATA_DIR):
        if file.endswith(".pdf"):
            full_path = DATA_DIR / file
            file_hash = hash_file(full_path)
            if manifest.get(file) == file_hash:
                continue
            extract_and_store(full_path, conn)
            manifest[file] = file_hash

    save_manifest(manifest)
    conn.close()
    print("🎯 Index build complete.")
