const Budget = require("../models/Budget");
const Transaction = require("../models/Transaction");

/**
 * Checks whether user can afford EMI based on active budget
 */
async function checkEmIAffordability({
  userId,
  emiAmount,
  category = "Overall",
}) {
  const now = new Date();
  category = category?.trim() || "Overall";

  let budget = await Budget.findOne({
    userId,
    category,
    startDate: { $lte: now },
    $or: [{ endDate: null }, { endDate: { $gte: now } }],
  });

  if (!budget && category !== "Overall") {
    budget = await Budget.findOne({
      userId,
      category: "Overall",
      startDate: { $lte: now },
      $or: [{ endDate: null }, { endDate: { $gte: now } }],
    });
  }

  if (!budget) {
    return {
      affordable: true,
      reason: "NO_BUDGET_DEFINED",
      emi: emiAmount,
    };
  }

  const spentAgg = await Transaction.aggregate([
    {
      $match: {
        userId,
        ...(budget.category !== "Overall" && { category: budget.category }),
        datetime: { $gte: budget.startDate, $lte: budget.endDate || now },
        price: { $lt: 0 },
      },
    },
    {
      $group: {
        _id: null,
        spent: { $sum: { $abs: "$price" } },
      },
    },
  ]);

  const spent = spentAgg[0]?.spent || 0;
  const projectedSpent = spent + emiAmount;
  const remaining = Math.max(budget.amount - projectedSpent, 0);

  const usagePercent =
    budget.amount > 0
      ? Number(((projectedSpent / budget.amount) * 100).toFixed(2))
      : 100;

  return {
    affordable: projectedSpent <= budget.amount,
    emi: emiAmount,
    budget: budget.amount,
    spent,
    projectedSpent,
    remaining,
    usagePercent,
    warning:
      usagePercent >= 100
        ? "BUDGET_EXCEEDED"
        : usagePercent >= 80
          ? "BUDGET_NEAR_LIMIT"
          : "SAFE",
  };
}

module.exports = { checkEmIAffordability };
