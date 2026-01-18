const express = require("express");
const router = express.Router();
const Transaction = require("../models/Transaction");
const authMiddleware = require("../middlewares/auth");

router.get("/summary", authMiddleware, async (req, res) => {
  const userId = req.user.userId;

  const data = await Transaction.aggregate([
    { $match: { userId } },
    {
      $group: {
        _id: null,
        income: {
          $sum: {
            $cond: [{ $gt: ["$price", 0] }, "$price", 0]
          }
        },
        expense: {
          $sum: {
            $cond: [{ $lt: ["$price", 0] }, { $abs: "$price" }, 0]
          }
        }
      }
    }
  ]);

  res.json({
    success: true,
    data: data[0] || { income: 0, expense: 0 }
  });
});

module.exports = router;
