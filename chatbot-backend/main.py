"""
FinTally — FastAPI main entry point.

Migrated from Node.js/Express. All previous Node routes are now handled here.
The chatbot and analytics routers were already in FastAPI and are preserved as-is.
"""

import logging
import os
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse

# Load environment variables from .env file
from dotenv import load_dotenv
load_dotenv()

# ── Cache ─────────────────────────────────────────────────────────────────────
from fastapi_cache import FastAPICache
from fastapi_cache.backends.inmemory import InMemoryBackend

# ── Existing FastAPI routers ──────────────────────────────────────────────────
from routers.analytics import router as analytics_router
from routers.chat import router as chat_router

# ── Newly migrated routers ────────────────────────────────────────────────────
from routers.auth import router as auth_router
from routers.budget import router as budget_router
from routers.emi import router as emi_router
from routers.feedback import router as feedback_router
from routers.transaction import router as transaction_router

# ── Recurring job scheduler ───────────────────────────────────────────────────
from cron_jobs.recurring_jobs import start_scheduler, stop_scheduler

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# ── Uploads directory ─────────────────────────────────────────────────────────
UPLOAD_DIR = os.path.join(os.path.dirname(__file__), "uploads")
os.makedirs(UPLOAD_DIR, exist_ok=True)


# ── Lifespan: startup / shutdown logic ─────────────────────────────────────────
@asynccontextmanager
async def lifespan(app: FastAPI):
    logger.info("🚀 Starting FinTally API...")

    # Start background scheduler
    start_scheduler()

    # Initialize cache (replaces @app.on_event("startup"))
    FastAPICache.init(InMemoryBackend(), prefix="finance-cache")

    # ── LLM warm-up ───────────────────────────────────────────────────────────
    # Load TinyLlama into memory at startup so the first chat request
    # doesn't block for 5-10 seconds. Runs in a thread to avoid
    # blocking the event loop.
    try:
        import asyncio
        import python_llama
        await asyncio.to_thread(python_llama.init, "tinyllama", 512)
        logger.info("✅ LLM loaded: tinyllama")
    except Exception as e:
        # Non-fatal — server still starts, LLM just loads on first request
        logger.warning(f"⚠️  LLM failed to pre-load: {e}")

    yield

    logger.info("🛑 Shutting down FinTally API...")
    stop_scheduler()


# ── App instance ──────────────────────────────────────────────────────────────
app = FastAPI(
    title="FinTally API",
    version="2.0.0",
    description="Full-stack personal finance tracker — FastAPI backend",
    lifespan=lifespan,
)

# ── CORS ──────────────────────────────────────────────────────────────────────
app.add_middleware(
    CORSMiddleware,
    allow_origins=[os.getenv("FRONTEND_URL", "http://localhost:5173")],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── Static files (/uploads) ───────────────────────────────────────────────────
app.mount("/uploads", StaticFiles(directory=UPLOAD_DIR), name="uploads")

# ── Routers ───────────────────────────────────────────────────────────────────
@app.get("/favicon.ico", include_in_schema=False)
async def favicon():
    return FileResponse(os.path.join(UPLOAD_DIR, "favicon.ico"))

app.include_router(auth_router)
app.include_router(transaction_router)
app.include_router(budget_router)
app.include_router(emi_router)
app.include_router(feedback_router)
app.include_router(chat_router)

# Fix from your old version: prefix + tags added
app.include_router(
    analytics_router,
    tags=["analytics"],
)

# ── Healthcheck ───────────────────────────────────────────────────────────────
@app.get("/api/test", tags=["Health"])
async def test():
    return {"body": "test ok"}

@app.get("/health", tags=["Health"])
async def health_check():
    return {"status": "ok", "message": "FinTally backend is running"}