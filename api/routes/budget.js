const express = require("express");
const router = express.Router();
const Budget = require("../models/Budget");
const Transaction = require("../models/Transaction");
const authMiddleware = require("../middlewares/auth");

// 🟢 Create or Update Budget
router.post("/", authMiddleware, async (req, res) => {
  try {
    const { amount, category, startDate, endDate, isRecurring } = req.body;
    const userId = req.user.userId;

    // 🟢 Calculate current spending in that category within the given period
    const matchQuery = {
      userId,
      ...(category && category !== "Overall" ? { category } : {}),
    };

    // Add date filter if provided
    if (startDate || endDate) {
      matchQuery.datetime = {};
      if (startDate) matchQuery.datetime.$gte = new Date(startDate);
      if (endDate) matchQuery.datetime.$lte = new Date(endDate);
    }

    const totalSpentResult = await Transaction.aggregate([
      { $match: matchQuery },
      { $group: { _id: null, total: { $sum: "$price" } } },
    ]);

    const spent = totalSpentResult[0]?.total || 0;

    // 🟠 Check if a budget already exists for the same category & active period
    const existing = await Budget.findOne({
      userId,
      category,
      endDate: { $gte: new Date() },
    });

    if (existing) {
      existing.amount = amount;
      existing.startDate = startDate;
      existing.endDate = endDate;
      existing.isRecurring = isRecurring;
      existing.spent = spent; // 🔄 Update spent when modifying existing
      await existing.save();
      return res.json({ success: true, data: existing });
    }

    // 🟣 Create new budget
    const budget = new Budget({
      userId,
      amount,
      category,
      startDate,
      endDate,
      isRecurring,
      spent, // 🔄 Set calculated spending
    });
    await budget.save();

    // ✅ Optional: also update future transactions automatically to sync
    await Budget.updateOne(
      {
        userId,
        category: category || "Overall",
        startDate: { $lte: new Date() },
        $or: [{ endDate: null }, { endDate: { $gte: new Date() } }],
      },
      { $set: { spent } }
    );

    res.json({ success: true, data: budget });
  } catch (error) {
    console.error("Budget creation error:", error);
    res.status(500).json({ success: false, message: "Server error" });
  }
});

// 🟠 Get Budgets for User
router.get("/", authMiddleware, async (req, res) => {
  try {
    const budgets = await Budget.find({ userId: req.user.userId }).sort({ createdAt: -1 });
    res.json({ success: true, data: budgets });
  } catch (error) {
    console.error("Error fetching budgets:", error);
    res.status(500).json({ success: false, message: "Server error" });
  }
});

// 🔵 Get Budget Summary (with current spending)
router.get("/summary", authMiddleware, async (req, res) => {
  try {
    const userId = req.user.userId;
    const budgets = await Budget.find({ userId });

    const summaries = [];

    for (const b of budgets) {
      const spent = await Transaction.aggregate([
        {
          $match: {
            userId: b.userId,
            category: b.category === "Overall" ? { $exists: true } : b.category,
            datetime: { $gte: b.startDate, $lte: b.endDate || new Date() },
          },
        },
        { $group: { _id: null, total: { $sum: "$price" } } },
      ]);

      const totalSpent = spent[0]?.total || 0;
      summaries.push({
        category: b.category,
        budget: b.amount,
        spent: totalSpent,
        remaining: b.amount - totalSpent,
        period: `${new Date(b.startDate).toLocaleDateString()} - ${b.endDate ? new Date(b.endDate).toLocaleDateString() : "Ongoing"}`,
      });
    }

    res.json({ success: true, data: summaries });
  } catch (error) {
    console.error("Error generating budget summary:", error);
    res.status(500).json({ success: false, message: "Server error" });
  }
});

// 🔴 Delete Budget
router.delete("/:id", authMiddleware, async (req, res) => {
  try {
    await Budget.findOneAndDelete({ _id: req.params.id, userId: req.user.userId });
    res.json({ success: true, message: "Budget deleted successfully" });
  } catch (error) {
    console.error("Error deleting budget:", error);
    res.status(500).json({ success: false, message: "Server error" });
  }
});

module.exports = router;
