from fastapi import FastAPI
from routers import analytics

app = FastAPI()

app.include_router(
    analytics.router,
    prefix="/analytics",
    tags=["analytics"],
)