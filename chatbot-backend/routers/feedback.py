import logging
from datetime import datetime

from fastapi import APIRouter, HTTPException

from schemas.models import FeedbackCreate
from services.db import db

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api", tags=["Feedback"])

feedback_col = db["feedbacks"]


# ──────────────────────────────────────────────
# POST /api/feedback
# ──────────────────────────────────────────────
@router.post("/feedback")
async def submit_feedback(body: FeedbackCreate):
    doc = {
        "name": body.name,
        "email": body.email,
        "message": body.message,
        "createdAt": datetime.utcnow(),
    }
    await feedback_col.insert_one(doc)
    return {"success": True, "message": "Feedback submitted successfully"}