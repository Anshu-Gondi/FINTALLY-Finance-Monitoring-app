# app/models/schemes.py
from pydantic import BaseModel
from typing import Optional
from datetime import datetime

class ChatInput(BaseModel):
    query: str

class ChatMessage(BaseModel):
    user: Optional[str] = "Anonymous"
    query: str
    response: str
    timestamp: datetime = datetime.utcnow()
    