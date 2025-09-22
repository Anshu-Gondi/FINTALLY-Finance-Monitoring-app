const express = require("express");
const router = express.Router();
const authMiddleware = require("../middlewares/auth");

const FASTAPI_BASE = process.env.FASTAPI_URL || "http://127.0.0.1:5000"; // 👈 update .env

// Generic proxy handler
const proxyFastAPI = async (req, res, fastapiPath, method = "GET", body = null) => {
  const url = `${FASTAPI_BASE}${fastapiPath}`;
  const headers = {
    'Authorization': req.headers['authorization'],
    "Content-Type": "application/json",
  };

  try {
    const options = {
      method,
      headers,
      ...(body && { body: JSON.stringify(body) }),
    };

    const response = await fetch(url, options);
    const data = await response.json();

    res.status(response.status).json(data);
  } catch (error) {
    console.error("FastAPI Chatbot Error:", error.message);
    res.status(500).json({ success: false, message: "Chatbot service error" });
  }
};

// 💬 Ask a question (POST)
router.post("/chat", authMiddleware, async (req, res) => {
  await proxyFastAPI(req, res, "/chat", "POST", req.body);
});

// 📄 List chats
router.get("/chats", authMiddleware, async (req, res) => {
  await proxyFastAPI(req, res, "/chats");
});

// 🧾 Get a specific chat
router.get("/chat/:id", authMiddleware, async (req, res) => {
  await proxyFastAPI(req, res, `/chat/${req.params.id}`);
});

// ❌ Delete a chat
router.delete("/chat/:id", authMiddleware, async (req, res) => {
  await proxyFastAPI(req, res, `/chat/${req.params.id}`, "DELETE");
});

module.exports = router;
