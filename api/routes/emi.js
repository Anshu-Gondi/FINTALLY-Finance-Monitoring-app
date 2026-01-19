const express = require("express");
const router = express.Router();
const authMiddleware = require("../middlewares/auth");
const Transaction = require("../models/Transaction");
const { checkEmIAffordability } = require("../services/emiBudget.service");

// Helper to calculate EMI in JS
function calculateEmi(principal, annualRate, months) {
  if (annualRate === 0) {
    return Number((principal / months).toFixed(2));
  }

  const r = annualRate / (12 * 100);
  const emi =
    (principal * r * Math.pow(1 + r, months)) / (Math.pow(1 + r, months) - 1);

  return Number(emi.toFixed(2));
}

function normalizeEmiInput({ principal, annualRate, months }) {
  const principalNum = Number(principal);
  const annualRateNum = Number(annualRate);
  const monthsNum = Number(months);

  if (
    !Number.isFinite(principalNum) ||
    !Number.isFinite(annualRateNum) ||
    !Number.isInteger(monthsNum) ||
    principalNum <= 0 ||
    annualRateNum <= 0 ||
    monthsNum <= 0
  ) {
    return null;
  }

  return {
    principalNum,
    annualRateNum,
    monthsNum,
    principalPaise: BigInt(Math.round(principalNum * 100)),
  };
}

// 🔹 Route: Just calculate EMI
router.post("/calculate", authMiddleware, (req, res) => {
  const input = normalizeEmiInput(req.body);

  if (!input) {
    return res.status(400).json({
      success: false,
      message: "Invalid numeric input",
    });
  }

  const { principalNum, annualRateNum, monthsNum } = input;

  const emi = calculateEmi(principalNum, annualRateNum, monthsNum);

  res.json({
    success: true,
    data: {
      emi,
      totalPayable: Number((emi * monthsNum).toFixed(2)),
      totalInterest: Number((emi * monthsNum - principalNum).toFixed(2)),
    },
  });
});

// 🔹 Route: Check EMI against budget
router.post("/check", authMiddleware, async (req, res) => {
  const { category = "Overall" } = req.body;
  const input = normalizeEmiInput(req.body);

  if (!input) {
    return res.status(400).json({
      success: false,
      message: "Invalid numeric input",
    });
  }

  const { principalNum, annualRateNum, monthsNum, principalPaise } = input;

  let budgetImpact;
  try {
    budgetImpact = await checkEmIAffordability({
      userId: req.user.userId,
      principalPaise,
      annualRate: annualRateNum,
      months: monthsNum,
      category,
    });
  } catch (err) {
    console.error("EMI budget check failed:", err);
    return res.status(500).json({
      success: false,
      message: "Failed to evaluate EMI affordability",
    });
  }

  const emi = calculateEmi(principalNum, annualRateNum, monthsNum);

  res.json({
    success: true,
    data: {
      emi,
      totalPayable: Number((emi * monthsNum).toFixed(2)),
      totalInterest: Number((emi * monthsNum - principalNum).toFixed(2)),
      budgetImpact,
    },
  });
});

// 🔹 Route: Create EMI as recurring transaction
router.post("/create", authMiddleware, async (req, res) => {
  const { category = "EMI", name = "Loan EMI" } = req.body;
  const input = normalizeEmiInput(req.body);

  if (!input) {
    return res.status(400).json({
      success: false,
      message: "Invalid numeric input",
    });
  }

  const { principalNum, annualRateNum, monthsNum, principalPaise } = input;

  let budgetImpact;
  try {
    budgetImpact = await checkEmIAffordability({
      userId: req.user.userId,
      principalPaise,
      annualRate: annualRateNum,
      months: monthsNum,
      category,
    });
  } catch (err) {
    console.error("EMI budget check failed:", err);
    return res.status(500).json({
      success: false,
      message: "Failed to evaluate EMI affordability",
    });
  }

  if (!budgetImpact.affordable) {
    return res.status(400).json({
      success: false,
      message: "EMI exceeds your budget",
      data: budgetImpact,
    });
  }

  const emi = calculateEmi(principalNum, annualRateNum, monthsNum);

  const emiTransaction = new Transaction({
    name,
    price: -emi, // expense
    description: `EMI for loan (${monthsNum} months @ ${annualRateNum}%)`,
    datetime: new Date(),
    category,
    userId: req.user.userId,
    isRecurring: true,
    recurringFrequency: "Monthly",
    lastGeneratedAt: null,
    emiMeta: {
      principal: principalNum,
      annualRate: annualRateNum,
      tenureMonths: monthsNum,
      originalTenure: monthsNum,
    },
  });

  await emiTransaction.save();

  res.json({
    success: true,
    message: "EMI added as recurring transaction",
    data: {
      emi,
      transactionId: emiTransaction._id,
      budgetImpact,
    },
  });
});

module.exports = router;
