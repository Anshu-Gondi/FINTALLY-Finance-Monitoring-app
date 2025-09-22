const express = require("express");
const router = express.Router();
const authMiddleware = require("../middlewares/auth");

const BASE_URL = process.env.DJANGO_URL;

const getDjangoData = async (endpoint, userId, res, token) => {
  try {
    const fastapiUrl = `${BASE_URL}${endpoint}?user_id=${userId}`;
    const response = await fetch(fastapiUrl, {
      headers: {
        "Authorization": `Bearer ${token}`,
      },
    });
    const data = await response.json();
    res.json(data);
  } catch (err) {
    res.status(500).json({ success: false, error: err.message });
  }
};

// insights.js (Node.js)
router.get("/insights/monthly", authMiddleware, async (req, res) => {
  const token = req.headers.authorization?.split(" ")[1];
  await getDjangoData("/monthly", req.user.userId, res, token);
});

router.get("/insights/weekly", authMiddleware, async (req, res) => {
  const token = req.headers.authorization?.split(" ")[1];
  await getDjangoData("/weekly", req.user.userId, res, token);
});

router.get("/insights/daily", authMiddleware, async (req, res) => {
  const token = req.headers.authorization?.split(" ")[1];
  await getDjangoData("/daily", req.user.userId, res, token);
});

router.get("/insights/max", authMiddleware, async (req, res) => {
  const token = req.headers.authorization?.split(" ")[1];
  await getDjangoData("/max", req.user.userId, res, token);
});

router.get("/insights/min", authMiddleware, async (req, res) => {
  const token = req.headers.authorization?.split(" ")[1];
  await getDjangoData("/min", req.user.userId, res, token);
});

router.get("/insights/trends", authMiddleware, async (req, res) => {
  const token = req.headers.authorization?.split(" ")[1];
  await getDjangoData("/trends", req.user.userId, res, token);
});

router.get("/insights/lifetime", authMiddleware, async (req, res) => {
  const token = req.headers.authorization?.split(" ")[1];
  await getDjangoData("/lifetime", req.user.userId, res, token);
});

router.get("/insights/categories", authMiddleware, async (req, res) => {
  const token = req.headers.authorization?.split(" ")[1];
  await getDjangoData("/categories", req.user.userId, res, token);
});

module.exports = router;