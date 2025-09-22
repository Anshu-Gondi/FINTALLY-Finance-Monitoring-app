# app/database/db.py
# app/database/db.py
from motor.motor_asyncio import AsyncIOMotorClient
import os
from dotenv import load_dotenv
import logging

load_dotenv()

MONGO_URL = os.getenv("MONGO_URL")

if not MONGO_URL:
    raise ValueError("❌ MONGO_URL is not set in environment variables")

try:
    client = AsyncIOMotorClient(MONGO_URL)
    db = client["tests"]
    chat_collection = db["chat_messages"]
    logging.info("✅ Connected to MongoDB successfully")
except Exception as e:
    logging.error(f"❌ MongoDB connection error: {e}")
    raise
