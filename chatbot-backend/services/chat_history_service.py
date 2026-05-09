"""
services/chat_history_service.py

MongoDB-backed chat history. One document per (user_id, session_id).
Messages appended in-place via $push + $slice — MongoDB handles trimming.

Document shape:
{
    "user_id":    str,
    "session_id": str,
    "messages": [
        {
            "role":      "user" | "assistant",
            "content":   str,
            "timestamp": float,
            "metadata":  {"tool_called": str|None, "tool_result": dict|None}
        }
    ],
    "created_at": float,
    "updated_at": float
}

Indexes to create once in DB setup:
    db.chat_history.create_index([("user_id", 1), ("session_id", 1)], unique=True)
    db.chat_history.create_index([("user_id", 1), ("updated_at", -1)])
"""

import time
import logging
from typing import Optional

from services.db import chat_history

logger = logging.getLogger(__name__)

DEFAULT_SESSION = "default"
MAX_HISTORY_LENGTH = 100


class ChatHistoryService:

    async def get_history(
        self,
        user_id: str,
        session_id: Optional[str] = None,
        limit: int = 20,
    ) -> list[dict]:
        """Stripped role+content only — what the LLM bridge needs."""
        sid = session_id or DEFAULT_SESSION
        doc = await chat_history.find_one(
            {"user_id": user_id, "session_id": sid},
            {"messages": {"$slice": -limit}},
        )
        if not doc:
            return []
        return [
            {"role": m["role"], "content": m["content"]}
            for m in doc.get("messages", [])
        ]

    async def get_full_history(
        self,
        user_id: str,
        session_id: Optional[str] = None,
        limit: int = 50,
    ) -> list[dict]:
        """Full objects with timestamps + metadata — for the /history endpoint."""
        sid = session_id or DEFAULT_SESSION
        doc = await chat_history.find_one(
            {"user_id": user_id, "session_id": sid},
            {"messages": {"$slice": -limit}},
        )
        if not doc:
            return []
        return doc.get("messages", [])

    async def get_all_sessions(self, user_id: str) -> list[str]:
        cursor = chat_history.find(
            {"user_id": user_id},
            {"session_id": 1, "updated_at": 1, "_id": 0},
        ).sort("updated_at", -1)
        docs = await cursor.to_list(length=50)
        return [d["session_id"] for d in docs]

    async def append_message(
        self,
        user_id: str,
        role: str,
        content: str,
        session_id: Optional[str] = None,
        metadata: Optional[dict] = None,
    ) -> None:
        sid = session_id or DEFAULT_SESSION
        now = time.time()
        await chat_history.update_one(
            {"user_id": user_id, "session_id": sid},
            {
                "$push": {
                    "messages": {
                        "$each": [{
                            "role": role,
                            "content": content,
                            "timestamp": now,
                            "metadata": metadata or {},
                        }],
                        "$slice": -MAX_HISTORY_LENGTH,
                    }
                },
                "$set": {"updated_at": now},
                "$setOnInsert": {
                    "user_id": user_id,
                    "session_id": sid,
                    "created_at": now,
                },
            },
            upsert=True,
        )

    async def clear_history(
        self, user_id: str, session_id: Optional[str] = None
    ) -> None:
        sid = session_id or DEFAULT_SESSION
        await chat_history.update_one(
            {"user_id": user_id, "session_id": sid},
            {"$set": {"messages": [], "updated_at": time.time()}},
        )

    async def delete_session(
        self, user_id: str, session_id: Optional[str] = None
    ) -> None:
        sid = session_id or DEFAULT_SESSION
        await chat_history.delete_one({"user_id": user_id, "session_id": sid})