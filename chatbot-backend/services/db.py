# services/db.py
import os
from datetime import datetime
from typing import List, Optional
from pydantic import BaseModel
from pymongo import AsyncMongoClient
from pymongo.server_api import ServerApi

class ChatMessage(BaseModel):
    role: str          # "user" or "assistant"
    content: str
    timestamp: datetime = datetime.utcnow()

class ChatHistory:
    def __init__(self):
        uri = os.getenv("MONGODB_URI", "mongodb://localhost:27017")
        self.client = AsyncMongoClient(uri, server_api=ServerApi('1'))
        self.db = self.client["fintally_db"]
        self.collection = self.db["chat_history"]

    async def add_message(self, session_id: str, message: ChatMessage):
        await self.collection.update_one(
            {"session_id": session_id},
            {"$push": {"messages": message.model_dump()}},
            upsert=True
        )

    async def get_history(self, session_id: str, limit: int = 20) -> List[ChatMessage]:
        doc = await self.collection.find_one({"session_id": session_id})
        if doc and "messages" in doc:
            return [ChatMessage(**msg) for msg in doc["messages"][-limit:]]
        return []

    async def close(self):
        self.client.close()
        
import os
from pymongo import MongoClient

client = MongoClient(os.getenv("MONGO_URL"))

db = client["test"]

transactions = db.transactions
budgets = db.budgets