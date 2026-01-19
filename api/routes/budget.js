const express = require("express");
const router = express.Router();
const Budget = require("../models/Budget");
const Transaction = require("../models/Transaction");
const authMiddleware = require("../middlewares/auth");

/* ================================
   CREATE OR UPDATE BUDGET
================================ */
router.post("/", authMiddleware, async (req, res) => {
  try {
    const { amount, category = "Overall", startDate, endDate, isRecurring } = req.body;
    const userId = req.user.userId;

    const start = startDate ? new Date(startDate) : new Date();
    const end = endDate ? new Date(endDate) : null;

    /* 🟢 Calculate spent (expenses only, normalized) */
    const matchQuery = {
      userId,
      price: { $lt: 0 },
      ...(category !== "Overall" && { category }),
      datetime: {
        $gte: start,
        ...(end && { $lte: end }),
      },
    };

    const spentAgg = await Transaction.aggregate([
      { $match: matchQuery },
      {
        $group: {
          _id: null,
          total: { $sum: { $abs: "$price" } },
        },
      },
    ]);

    const spent = spentAgg[0]?.total || 0;

    /* 🟠 Prevent overlapping budgets */
    const existing = await Budget.findOne({
      userId,
      category,
      startDate: { $lte: end || new Date() },
      $or: [{ endDate: null }, { endDate: { $gte: start } }],
    });

    if (existing) {
      existing.amount = amount;
      existing.startDate = start;
      existing.endDate = end;
      existing.isRecurring = isRecurring;
      existing.spent = spent;
      await existing.save();

      return res.json({ success: true, data: existing });
    }

    /* 🟣 Create new budget */
    const budget = new Budget({
      userId,
      amount,
      category,
      startDate: start,
      endDate: end,
      isRecurring,
      spent,
    });

    await budget.save();
    res.json({ success: true, data: budget });
  } catch (error) {
    console.error("Budget creation error:", error);
    res.status(500).json({ success: false, message: "Server error" });
  }
});

/* ================================
   GET USER BUDGETS
================================ */
router.get("/", authMiddleware, async (req, res) => {
  try {
    const budgets = await Budget.find({ userId: req.user.userId })
      .sort({ createdAt: -1 });

    res.json({ success: true, data: budgets });
  } catch (error) {
    console.error("Error fetching budgets:", error);
    res.status(500).json({ success: false, message: "Server error" });
  }
});

/* ================================
   BUDGET SUMMARY (SINGLE AGG)
================================ */
router.get("/summary", authMiddleware, async (req, res) => {
  try {
    const userId = req.user.userId;
    const now = new Date();

    const budgets = await Budget.find({ userId });

    /* 🔥 Aggregate expenses ONCE */
    const expenses = await Transaction.aggregate([
      {
        $match: {
          userId,
          price: { $lt: 0 },
        },
      },
      {
        $group: {
          _id: "$category",
          total: { $sum: { $abs: "$price" } },
        },
      },
    ]);

    const expenseMap = {};
    for (const e of expenses) {
      expenseMap[e._id] = e.total;
    }

    const summaries = budgets.map((b) => {
      const spent =
        b.category === "Overall"
          ? Object.values(expenseMap).reduce((a, b) => a + b, 0)
          : expenseMap[b.category] || 0;

      return {
        category: b.category,
        budget: b.amount,
        spent,
        remaining: Math.max(b.amount - spent, 0),
        period: `${new Date(b.startDate).toLocaleDateString()} - ${
          b.endDate ? new Date(b.endDate).toLocaleDateString() : "Ongoing"
        }`,
      };
    });

    res.json({ success: true, data: summaries });
  } catch (error) {
    console.error("Error generating budget summary:", error);
    res.status(500).json({ success: false, message: "Server error" });
  }
});

/* ================================
   DELETE BUDGET
================================ */
router.delete("/:id", authMiddleware, async (req, res) => {
  try {
    await Budget.findOneAndDelete({
      _id: req.params.id,
      userId: req.user.userId,
    });

    res.json({ success: true, message: "Budget deleted successfully" });
  } catch (error) {
    console.error("Error deleting budget:", error);
    res.status(500).json({ success: false, message: "Server error" });
  }
});

module.exports = router;
