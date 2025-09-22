import subprocess
import sys
from fastapi import APIRouter, Depends, HTTPException
from datetime import datetime
from bson import ObjectId

from ..models.schemas import ChatInput, ChatMessage
from ..database.db import chat_collection
from ..auth.utils import get_current_user  # Protect endpoints
from ..services.rag_engine import ask_with_context

router = APIRouter()


# Convert MongoDB ObjectId to string
def serialize_chat(chat):
    chat["_id"] = str(chat["_id"])
    return chat


@router.post("/chat")
async def chat(input: ChatInput, user=Depends(get_current_user)):
    """
    Ask the chatbot with context and save the chat in MongoDB
    """
    response = ask_with_context(input.query)

    chat_doc = {
        "user": user.get("email", "Anonymous"),
        "query": input.query,
        "response": response,
        "timestamp": datetime.utcnow()
    }

    result = await chat_collection.insert_one(chat_doc)

    if not result.inserted_id:
        raise HTTPException(status_code=500, detail="Failed to save chat.")

    return {"response": response, "id": str(result.inserted_id)}


@router.get("/chats")
async def get_all_chats(user=Depends(get_current_user)):
    """
    Get all chats for the current user
    """
    chats = []
    async for chat in chat_collection.find({"user": user.get("email", "Anonymous")}).sort("timestamp", -1):
        chats.append(serialize_chat(chat))
    return chats


@router.get("/chat/{chat_id}")
async def get_chat_by_id(chat_id: str, user=Depends(get_current_user)):
    """
    Get a single chat by its ID
    """
    chat = await chat_collection.find_one({"_id": ObjectId(chat_id)})

    if not chat:
        raise HTTPException(status_code=404, detail="Chat not found.")

    return serialize_chat(chat)


@router.delete("/chat/{chat_id}")
async def delete_chat_by_id(chat_id: str, user=Depends(get_current_user)):
    """
    Delete a chat by its ID
    """
    result = await chat_collection.delete_one({"_id": ObjectId(chat_id)})

    if result.deleted_count == 0:
        raise HTTPException(status_code=404, detail="Chat not found.")

    return {"deleted": True}

@router.post("/refresh-index")
def refresh_index():
    subprocess.run([sys.executable, "build_index.py"])
    return {"message": "Index rebuild triggered"}
