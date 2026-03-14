# services/db.py

import os
from motor.motor_asyncio import AsyncIOMotorClient

MONGO_URL = os.getenv("MONGO_URL", "mongodb://localhost:27017")

client = AsyncIOMotorClient(MONGO_URL)

# Your existing database
db = client["test"]

# Collections
transactions = db["transactions"]
budgets = db["budgets"]
chat_history = db["chat_history"]