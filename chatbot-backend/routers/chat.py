"""
routers/chat.py

FastAPI chat router. Endpoints:
  POST   /api/chat          — streaming SSE (primary)
  POST   /api/chat/once     — non-streaming (testing/webhooks)
  GET    /api/chat/history  — full history with timestamps + tool metadata
  GET    /api/chat/sessions — list all sessions for user
  DELETE /api/chat/history  — clear a session
"""

import json
import logging
from typing import Optional

from fastapi import APIRouter, Depends
from fastapi.responses import StreamingResponse
from pydantic import BaseModel, Field

from services.llm_bridge import chat_stream, chat_once
from services.chat_history_service import ChatHistoryService
from dependencies.auth import get_current_user

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/chat", tags=["Chat"])


# ──────────────────────────────────────────────────────────────────────────────
# Schemas
# ──────────────────────────────────────────────────────────────────────────────

class ChatRequest(BaseModel):
    message: str = Field(..., min_length=1, max_length=2000)
    session_id: Optional[str] = None
    max_tokens: int = Field(default=512, ge=64, le=1024)


class ChatOnceResponse(BaseModel):
    reply: str
    tool_called: Optional[str] = None
    tool_result: Optional[dict] = None


# ──────────────────────────────────────────────────────────────────────────────
# Streaming endpoint
# ──────────────────────────────────────────────────────────────────────────────

@router.post("/")
async def chat_endpoint(
    body: ChatRequest,
    current_user: dict = Depends(get_current_user),
):
    """
    Streaming chat via Server-Sent Events.

    SSE event types:
      data: <token>              — LLM token, render directly
      data: [CONTEXT_LOADED]    — user financial data fetched (show subtle indicator)
      data: [TOOL_CALL:name]    — tool executing (show spinner)
      data: [TOOL_RESULT:{...}] — raw tool output (render as card if desired)
      data: [ERROR:msg]         — failure
      data: [DONE]              — stream complete
    """
    user_id = str(current_user["id"])
    history_svc = ChatHistoryService()
    history = await history_svc.get_history(user_id, session_id=body.session_id)

    # Persist user message immediately
    await history_svc.append_message(
        user_id=user_id,
        role="user",
        content=body.message,
        session_id=body.session_id,
    )

    assistant_chunks: list[str] = []
    tool_called_name: Optional[str] = None
    tool_result_data: Optional[dict] = None

    async def event_stream():
        nonlocal tool_called_name, tool_result_data
        try:
            async for chunk in chat_stream(
                user_id=user_id,
                user_message=body.message,
                chat_history=history,
                max_tokens=body.max_tokens,
            ):
                if chunk.startswith("[TOOL_CALL:"):
                    tool_called_name = chunk[len("[TOOL_CALL:"):-1]
                    yield f"data: {chunk}\n\n"

                elif chunk.startswith("[TOOL_RESULT:"):
                    try:
                        tool_result_data = json.loads(chunk[len("[TOOL_RESULT:"):-1])
                    except Exception:
                        pass
                    yield f"data: {chunk}\n\n"

                elif chunk.startswith("[CONTEXT_LOADED]"):
                    yield f"data: {chunk}\n\n"

                elif chunk.startswith("[ERROR:"):
                    yield f"data: {chunk}\n\n"

                else:
                    assistant_chunks.append(chunk)
                    yield f"data: {chunk}\n\n"

        except Exception as e:
            logger.error(f"Stream error user={user_id}: {e}")
            yield f"data: [ERROR:{e}]\n\n"

        finally:
            if assistant_chunks:
                await history_svc.append_message(
                    user_id=user_id,
                    role="assistant",
                    content="".join(assistant_chunks),
                    session_id=body.session_id,
                    metadata={
                        "tool_called": tool_called_name,
                        "tool_result": tool_result_data,
                    },
                )
            yield "data: [DONE]\n\n"

    return StreamingResponse(
        event_stream(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no",
        },
    )


# ──────────────────────────────────────────────────────────────────────────────
# Non-streaming endpoint
# ──────────────────────────────────────────────────────────────────────────────

@router.post("/once", response_model=ChatOnceResponse)
async def chat_once_endpoint(
    body: ChatRequest,
    current_user: dict = Depends(get_current_user),
):
    user_id = str(current_user["id"])
    history_svc = ChatHistoryService()
    history = await history_svc.get_history(user_id, session_id=body.session_id)

    await history_svc.append_message(
        user_id=user_id, role="user", content=body.message, session_id=body.session_id
    )

    tool_called: Optional[str] = None
    tool_result: Optional[dict] = None
    reply_chunks: list[str] = []

    async for chunk in chat_stream(
        user_id=user_id,
        user_message=body.message,
        chat_history=history,
        max_tokens=body.max_tokens,
    ):
        if chunk.startswith("[TOOL_CALL:"):
            tool_called = chunk[len("[TOOL_CALL:"):-1]
        elif chunk.startswith("[TOOL_RESULT:"):
            try:
                tool_result = json.loads(chunk[len("[TOOL_RESULT:"):-1])
            except Exception:
                pass
        elif not chunk.startswith("["):
            reply_chunks.append(chunk)

    reply = "".join(reply_chunks)
    await history_svc.append_message(
        user_id=user_id,
        role="assistant",
        content=reply,
        session_id=body.session_id,
        metadata={"tool_called": tool_called, "tool_result": tool_result},
    )

    return ChatOnceResponse(reply=reply, tool_called=tool_called, tool_result=tool_result)


# ──────────────────────────────────────────────────────────────────────────────
# History & sessions
# ──────────────────────────────────────────────────────────────────────────────

@router.get("/history")
async def get_chat_history(
    session_id: Optional[str] = None,
    limit: int = 50,
    current_user: dict = Depends(get_current_user),
):
    user_id = str(current_user["id"])
    history_svc = ChatHistoryService()
    history = await history_svc.get_full_history(user_id, session_id=session_id, limit=limit)
    return {"history": history, "count": len(history)}


@router.get("/sessions")
async def get_sessions(current_user: dict = Depends(get_current_user)):
    user_id = str(current_user["id"])
    history_svc = ChatHistoryService()
    sessions = await history_svc.get_all_sessions(user_id)
    return {"sessions": sessions}


@router.delete("/history")
async def clear_chat_history(
    session_id: Optional[str] = None,
    current_user: dict = Depends(get_current_user),
):
    user_id = str(current_user["id"])
    history_svc = ChatHistoryService()
    await history_svc.clear_history(user_id, session_id=session_id)
    return {"message": "Chat history cleared"}