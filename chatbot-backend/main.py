from fastapi import FastAPI
from fastapi_cache import FastAPICache
from fastapi_cache.backends.inmemory import InMemoryBackend
from routers import analytics

app = FastAPI()


@app.on_event("startup")
async def startup():
    FastAPICache.init(InMemoryBackend(), prefix="finance-cache")

app.include_router(
    analytics.router,
    prefix="/analytics",
    tags=["analytics"],
)