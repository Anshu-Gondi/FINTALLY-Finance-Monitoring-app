# app/main.py
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from app.routes import chatbot

app = FastAPI(title="Finance Chatbot")

# Allow requests from your frontend
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Your React app origin
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(chatbot.router)

@app.get("/")
def home():
    return {"message": "Welcome to the Finance Chatbot API"}