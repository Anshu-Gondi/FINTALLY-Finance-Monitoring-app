const express = require("express");
const router = express.Router();
const authMiddleware = require("../middlewares/auth");

const BASE_URL = process.env.ANALYTICS_URL;

const getAnalyticsData = async (endpoint, req, res) => {
  try {
    const token = req.headers.authorization?.split(" ")[1];

    const fastapiUrl = `${BASE_URL}${endpoint}`;

    const response = await fetch(fastapiUrl, {
      headers: {
        Authorization: `Bearer ${token}`,
      },
    });

    const data = await response.json();
    res.json(data);
  } catch (err) {
    console.error("Analytics error:", err);
    res.status(500).json({ success: false, error: err.message });
  }
};

// insights.js (Node.js)
router.get("/insights/period", authMiddleware, async (req, res) => {
  const range = req.query.range || "weekly";
  await getAnalyticsData(`/period-summary?range=${range}`, req, res);
});

router.get("/insights/daily", authMiddleware, async (req, res) => {
  await getAnalyticsData("/daily-summary", req, res);
});

router.get("/insights/trends", authMiddleware, async (req, res) => {
  const range = req.query.range || "6months";

  await getAnalyticsData(`/trend-summary?range=${range}`, req, res);
});

router.get("/insights/trends", authMiddleware, async (req, res) => {
  const range = req.query.range || "6months";

  await getAnalyticsData(`/trend-summary?range=${range}`, req, res);
});

router.get("/insights/lifetime", authMiddleware, async (req, res) => {
  await getAnalyticsData("/lifetime-analysis", req, res);
});

router.get("/insights/categories", authMiddleware, async (req, res) => {
  const { start, end, type, keyword, limit } = req.query;

  let query = new URLSearchParams({
    start,
    end,
    type,
    keyword,
    limit,
  }).toString();

  await getAnalyticsData(`/category-summary?${query}`, req, res);
});

router.get("/insights/emi-pressure", authMiddleware, async (req, res) => {
  await getAnalyticsData("/emi-pressure", req, res);
});

router.get("/insights/cashflow", authMiddleware, async (req, res) => {
  const horizons = req.query.horizons || "30,60,90";

  await getAnalyticsData(`/cashflow-forecast?horizons=${horizons}`, req, res);
});

router.get("/insights/budget-breach", authMiddleware, async (req, res) => {
  const { end_date } = req.query;

  await getAnalyticsData(`/budget-breach?end_date=${end_date}`, req, res);
});

router.get("/insights/budget-breach", authMiddleware, async (req, res) => {
  const { end_date } = req.query;

  await getAnalyticsData(`/budget-breach?end_date=${end_date}`, req, res);
});

router.get("/insights/anomalies", authMiddleware, async (req, res) => {
  const threshold = req.query.threshold || 2.5;

  await getAnalyticsData(`/anomalies?threshold=${threshold}`, req, res);
});

router.get("/insights/savings", authMiddleware, async (req, res) => {
  await getAnalyticsData("/savings-optimization", req, res);
});

router.get("/insights/goal-projection", authMiddleware, async (req, res) => {
  const { target } = req.query;

  await getAnalyticsData(`/goal-projection?target_amount=${target}`, req, res);
});

module.exports = router;
