import os
import logging
from datetime import datetime, timedelta, timezone

import bcrypt
import jwt
from fastapi import APIRouter, Depends, HTTPException, status
from google.auth.transport import requests as google_requests
from google.oauth2 import id_token
from motor.motor_asyncio import AsyncIOMotorDatabase

from dependencies.auth import get_current_user
from schemas.models import AuthResponse, GoogleAuthRequest, LoginRequest, SignupRequest
from services.db import db

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api", tags=["Auth"])

JWT_SECRET = os.getenv("JWT_SECRET", "secret123")
JWT_ALGORITHM = "HS256"
JWT_EXPIRE_DAYS = 7
GOOGLE_CLIENT_ID = os.getenv("GOOGLE_CLIENT_ID", "")

users = db["users"]


def _make_token(user_id: str) -> str:
    payload = {
        "userId": user_id,
        "exp": datetime.now(timezone.utc) + timedelta(days=JWT_EXPIRE_DAYS),
    }
    return jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)


# ──────────────────────────────────────────────
# POST /api/signup
# ──────────────────────────────────────────────
@router.post("/signup", response_model=AuthResponse)
async def signup(body: SignupRequest):
    existing = await users.find_one({"email": body.email})
    if existing:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Email already registered",
        )

    hashed = bcrypt.hashpw(body.password.encode(), bcrypt.gensalt()).decode()
    result = await users.insert_one(
        {
            "name": body.name,
            "email": body.email,
            "password": hashed,
            "authProvider": "local",
        }
    )
    logger.info("New user signed up: %s", body.email)
    return AuthResponse(success=True, message="User created successfully")


# ──────────────────────────────────────────────
# POST /api/login
# ──────────────────────────────────────────────
@router.post("/login", response_model=AuthResponse)
async def login(body: LoginRequest):
    user = await users.find_one({"email": body.email})
    if not user:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid email or password",
        )

    if not bcrypt.checkpw(body.password.encode(), user["password"].encode()):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid email or password",
        )

    token = _make_token(str(user["_id"]))
    return AuthResponse(success=True, token=token)


# ──────────────────────────────────────────────
# POST /api/google-auth
# ──────────────────────────────────────────────
@router.post("/google-auth", response_model=AuthResponse)
async def google_auth(body: GoogleAuthRequest):
    try:
        payload = id_token.verify_oauth2_token(
            body.token,
            google_requests.Request(),
            GOOGLE_CLIENT_ID,
        )
    except Exception as exc:
        logger.warning("Google token verification failed: %s", exc)
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid Google token",
        )

    email = payload["email"]
    name = payload.get("name", email)

    user = await users.find_one({"email": email})
    if not user:
        result = await users.insert_one(
            {"name": name, "email": email, "authProvider": "google"}
        )
        user_id = str(result.inserted_id)
    else:
        user_id = str(user["_id"])

    token = _make_token(user_id)
    return AuthResponse(success=True, token=token)