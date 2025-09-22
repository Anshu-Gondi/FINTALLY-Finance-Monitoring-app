# app/services/db_service.py
from ..database.db import chat_collection
from ..models.schemas import ChatMessage
from bson import ObjectId

def serialize_chat(chat):
    """Convert MongoDB chat doc to JSON-safe dict."""
    if not chat:
        return None
    chat["_id"] = str(chat["_id"])
    return chat

async def save_chat(message: ChatMessage):
    message_dict = message.dict()
    result = await chat_collection.insert_one(message_dict)
    return str(result.inserted_id)

async def get_chat(chat_id: str):
    chat = await chat_collection.find_one({"_id": ObjectId(chat_id)})
    return serialize_chat(chat)

async def list_chats(user_email: str):
    chats = []
    async for chat in chat_collection.find({"user": user_email}).sort("timestamp", -1):
        chats.append(serialize_chat(chat))
    return chats

async def delete_chat(chat_id: str):
    result = await chat_collection.delete_one({"_id": ObjectId(chat_id)})
    return result.deleted_count
