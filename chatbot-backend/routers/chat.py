# routers/chat.py
"""
FastAPI Chat Router
──────────────────────────────────────────────────────────────────────────────
Orchestrates secure user communication channels across streaming SSE 
and stateless non-streaming REST endpoints.
"""

import json
import logging
from typing import Optional

from fastapi import APIRouter, Depends, BackgroundTasks
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
# Streaming Endpoint (SSE)
# ──────────────────────────────────────────────────────────────────────────────

@router.post("/")
async def chat_endpoint(
    body: ChatRequest,
    background_tasks: BackgroundTasks,
    current_user: dict = Depends(get_current_user),
):
    """
    Streaming chat via Server-Sent Events (SSE).
    """
    # ─── Robust User ID Extraction ──────────────────────────────────────────
    if isinstance(current_user, dict):
        user_id = str(
            current_user.get("userId") or
            current_user.get("_id") or
            current_user.get("id") or
            current_user.get("user_id")
        )
    else:
        user_id = str(
            getattr(current_user, "userId", None) or
            getattr(current_user, "_id", None) or
            getattr(current_user, "id", None) or
            getattr(current_user, "user_id", None)
        )

    if not user_id or user_id == "None":
        logger.error(f"Failed to isolate dynamic ID signature from user object schema context: {current_user}")
        from fastapi import HTTPException
        raise HTTPException(status_code=401, detail="Invalid user authentication schema payload structural context")
    # ────────────────────────────────────────────────────────────────────────

    history_svc = ChatHistoryService()

    # 1. Pull chat history context window for the LLM
    history = await history_svc.get_history(user_id, session_id=body.session_id)

    # 2. Append incoming user query to history immediately
    await history_svc.append_message(
        user_id=user_id,
        role="user",
        content=body.message,
        session_id=body.session_id,
    )

    # Track operational states outside generator frame boundaries
    assistant_chunks: list[str] = []
    state = {"tool_name": None, "tool_result": None}

    async def event_stream():
        try:
            print("\n🚀 [DEBUG] SSE stream connected. Requesting chat_stream from LLM bridge...")
            
            async for chunk in chat_stream(
                user_id=user_id,
                user_message=body.message,
                chat_history=history,
                max_tokens=body.max_tokens,
            ):
                print(f"📦 [DEBUG] Received raw chunk from LLM bridge: {repr(chunk)}")
                
                if chunk.startswith("[TOOL_CALL:"):
                    state["tool_name"] = chunk[len("[TOOL_CALL:"):-1]
                    print(f"🛠️ [DEBUG] Tool Call detected: {state['tool_name']}")
                elif chunk.startswith("[TOOL_RESULT:"):
                    try:
                        state["tool_result"] = json.loads(chunk[len("[TOOL_RESULT:"):-1])
                        print(f"✅ [DEBUG] Tool Result parsed cleanly: {state['tool_result']}")
                    except Exception as parse_err:
                        print(f"❌ [DEBUG] Failed to parse tool result json: {parse_err}")
                elif not chunk.startswith("[") or chunk.startswith("[ERROR:"):
                    if not chunk.startswith("[ERROR:"):
                        assistant_chunks.append(chunk)

                # Stream out clean compliant SSE formats
                yield f"data: {chunk}\n\n"

        except Exception as e:
            print(f"💥 [DEBUG] CRITICAL EXPLOSION inside event_stream generator loop: {e}")
            logger.error(f"Stream exception encountered for user={user_id}: {e}")
            yield f"data: [ERROR:Internal streaming collapse — {e}]\n\n"

        finally:
            print(f"🏁 [DEBUG] event_stream hit finally block. Total text chunks captured: {len(assistant_chunks)}")
            if assistant_chunks:
                background_tasks.add_task(
                    history_svc.append_message,
                    user_id=user_id,
                    role="assistant",
                    content="".join(assistant_chunks),
                    session_id=body.session_id,
                    metadata={
                        "tool_called": state["tool_name"],
                        "tool_result": state["tool_result"],
                    }
                )
            yield "data: [DONE]\n\n"

    return StreamingResponse(
        event_stream(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no",
            "Connection": "keep-alive",
        },
    )


# ──────────────────────────────────────────────────────────────────────────────
# Non-Streaming Endpoint (Stateless / Testing)
# ──────────────────────────────────────────────────────────────────────────────

@router.post("/once", response_model=ChatOnceResponse)
async def chat_once_endpoint(
    body: ChatRequest,
    current_user: dict = Depends(get_current_user),
):
    """
    Non-streaming endpoint optimized for webhooks and automated testing environments.
    """
    # ─── Robust User ID Extraction ──────────────────────────────────────────
    if isinstance(current_user, dict):
        user_id = str(
            current_user.get("userId") or
            current_user.get("_id") or
            current_user.get("id") or
            current_user.get("user_id")
        )
    else:
        user_id = str(
            getattr(current_user, "userId", None) or
            getattr(current_user, "_id", None) or
            getattr(current_user, "id", None) or
            getattr(current_user, "user_id", None)
        )

    if not user_id or user_id == "None":
        logger.error(f"Failed to isolate dynamic ID signature from user object schema context: {current_user}")
        from fastapi import HTTPException
        raise HTTPException(status_code=401, detail="Invalid user authentication schema payload structural context")
    # ────────────────────────────────────────────────────────────────────────

    history_svc = ChatHistoryService()

    # 1. Fetch current context history
    history = await history_svc.get_history(user_id, session_id=body.session_id)

    # 2. Log user message state
    await history_svc.append_message(
        user_id=user_id, role="user", content=body.message, session_id=body.session_id
    )

    # 3. Use your dedicated non-streaming bridge method directly!
    reply = await chat_once(
        user_id=user_id,
        user_message=body.message,
        chat_history=history,
        max_tokens=body.max_tokens,
    )

    tool_called = None
    tool_result = None

    # Save finalized output cleanly
    await history_svc.append_message(
        user_id=user_id,
        role="assistant",
        content=reply,
        session_id=body.session_id,
        metadata={"tool_called": tool_called, "tool_result": tool_result},
    )

    return ChatOnceResponse(reply=reply, tool_called=tool_called, tool_result=tool_result)


# ──────────────────────────────────────────────────────────────────────────────
# Management Endpoints
# ──────────────────────────────────────────────────────────────────────────────

@router.get("/history")
async def get_chat_history(
    session_id: Optional[str] = None,
    limit: int = 50,
    current_user: dict = Depends(get_current_user),
):
    # ─── Robust User ID Extraction ──────────────────────────────────────────
    if isinstance(current_user, dict):
        user_id = str(
            current_user.get("userId") or
            current_user.get("_id") or
            current_user.get("id") or
            current_user.get("user_id")
        )
    else:
        user_id = str(
            getattr(current_user, "userId", None) or
            getattr(current_user, "_id", None) or
            getattr(current_user, "id", None) or
            getattr(current_user, "user_id", None)
        )

    if not user_id or user_id == "None":
        logger.error(f"Failed to isolate dynamic ID signature from user object schema context: {current_user}")
        from fastapi import HTTPException
        raise HTTPException(status_code=401, detail="Invalid user authentication schema payload structural context")
    # ────────────────────────────────────────────────────────────────────────
    history_svc = ChatHistoryService()
    history = await history_svc.get_full_history(user_id, session_id=session_id, limit=limit)
    return {"history": history, "count": len(history)}


@router.get("/sessions")
async def get_sessions(current_user: dict = Depends(get_current_user)):
    # ─── Robust User ID Extraction ──────────────────────────────────────────
    if isinstance(current_user, dict):
        user_id = str(
            current_user.get("userId") or
            current_user.get("_id") or
            current_user.get("id") or
            current_user.get("user_id")
        )
    else:
        user_id = str(
            getattr(current_user, "userId", None) or
            getattr(current_user, "_id", None) or
            getattr(current_user, "id", None) or
            getattr(current_user, "user_id", None)
        )

    if not user_id or user_id == "None":
        logger.error(f"Failed to isolate dynamic ID signature from user object schema context: {current_user}")
        from fastapi import HTTPException
        raise HTTPException(status_code=401, detail="Invalid user authentication schema payload structural context")
    # ────────────────────────────────────────────────────────────────────────
    history_svc = ChatHistoryService()
    sessions = await history_svc.get_all_sessions(user_id)
    return {"sessions": sessions}


@router.delete("/history")
async def clear_chat_history(
    session_id: Optional[str] = None,
    current_user: dict = Depends(get_current_user),
):
    # ─── Robust User ID Extraction ──────────────────────────────────────────
    if isinstance(current_user, dict):
        user_id = str(
            current_user.get("userId") or
            current_user.get("_id") or
            current_user.get("id") or
            current_user.get("user_id")
        )
    else:
        user_id = str(
            getattr(current_user, "userId", None) or
            getattr(current_user, "_id", None) or
            getattr(current_user, "id", None) or
            getattr(current_user, "user_id", None)
        )

    if not user_id or user_id == "None":
        logger.error(f"Failed to isolate dynamic ID signature from user object schema context: {current_user}")
        from fastapi import HTTPException
        raise HTTPException(status_code=401, detail="Invalid user authentication schema payload structural context")
    # ────────────────────────────────────────────────────────────────────────

    history_svc = ChatHistoryService()
    await history_svc.clear_history(user_id, session_id=session_id)
    return {"message": "Chat history cleared successfully"}