# services/chat_history_service.py

from datetime import datetime
from typing import List
from pydantic import BaseModel
from services.db import chat_history


class ChatMessage(BaseModel):
    role: str
    content: str
    timestamp: datetime = datetime.utcnow()


class ChatHistoryService:

    async def add_message(self, session_id: str, message: ChatMessage):

        await chat_history.update_one(
            {"session_id": session_id},
            {"$push": {"messages": message.model_dump()}},
            upsert=True
        )

    async def get_history(self, session_id: str, limit: int = 20):

        doc = await chat_history.find_one({"session_id": session_id})

        if doc and "messages" in doc:
            return doc["messages"][-limit:]

        return []