const Budget = require("../models/Budget");
const Transaction = require("../models/Transaction");
const financeCpp = require("../finance-cpp-addon");

/**
 * Safely converts a value to BigInt
 */
function toBigIntSafe(value) {
  if (value == null) throw new Error("Value cannot be null or undefined for BigInt conversion");
  try {
    return BigInt(value);
  } catch (err) {
    throw new Error(`Invalid BigInt value: ${value}`);
  }
}

/**
 * Checks whether user can afford EMI based on active budget
 */
async function checkEmIAffordability({
  userId,
  principalPaise,
  annualRate,
  months,
  category = "Overall",
}) {
  // 🔹 Validate inputs before calling C++ addon
  if (!userId) throw new Error("Missing userId");
  if (principalPaise == null || annualRate == null || months == null) {
    throw new Error(
      `Invalid EMI parameters: principalPaise=${principalPaise}, annualRate=${annualRate}, months=${months}`
    );
  }

  const now = new Date();
  category = category?.trim() || "Overall";

  // 🔥 Compute EMI using C++ addon safely
  let emiAmount;
  try {
    emiAmount = financeCpp.calculateEmi({
      principalPaise: toBigIntSafe(principalPaise),
      annualRate: Number(annualRate),
      months: Number(months),
    });
  } catch (err) {
    console.error("Error calculating EMI via C++ addon:", err);
    throw new Error("Failed to calculate EMI");
  }

  // 🔹 Find active budget
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

  // 🔹 Aggregate spent in this budget
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
