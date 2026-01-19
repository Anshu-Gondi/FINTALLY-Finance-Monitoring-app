from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from typing import List
from services.db import ChatHistory, ChatMessage
from services.llm import call_llm_rust  # ← your wrapper for fintally_chatbot.chat(...)
import asyncio

router = APIRouter(prefix="/chat", tags=["chat"])

class ChatRequest(BaseModel):
    session_id: str          # Unique per user/session (e.g., UUID or user_id)
    query: str
    max_tokens: int = 128

class ChatResponse(BaseModel):
    response: str
    history: List[ChatMessage]  # Optional: return updated history

db = ChatHistory()  # Singleton instance

@router.post("/", response_model=ChatResponse)
async def chat_with_memory(request: ChatRequest):
    try:
        # 1. Load recent history
        history = await db.get_history(request.session_id)

        # 2. Build full context prompt (system + history + new query)
        context = "You are FinTally's finance assistant. Use tools for math.\n\n"
        for msg in history:
            context += f"{msg.role.capitalize()}: {msg.content}\n"
        context += f"User: {request.query}\n