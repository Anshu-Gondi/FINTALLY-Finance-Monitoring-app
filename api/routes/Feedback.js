const express = require("express");
const router = express.Router();
const Feedback = require("../models/Feedback");

// POST /api/feedback — Save feedback to DB
router.post("/feedback", async (req, res) => {
  try {
    const { name, email, message } = req.body;

    if (!name || !email || !message) {
      return res.status(400).json({ error: "All fields are required" });
    }

    const feedback = new Feedback({ name, email, message });
    await feedback.save();

    res.json({ success: true, message: "Feedback submitted successfully" });
  } catch (error) {
    console.error("Error saving feedback:", error);
    res.status(500).json({ error: "Server error" });
  }
});

module.exports = router;
