const express = require("express");
const router = express.Router();
const authMiddleware = require("../middlewares/auth");
const Transaction = require("../models/Transaction");
const { checkEmIAffordability } = require("../services/emiBudget.service");

// Helper to calculate EMI in JS
function calculateEmi(principal, annualRate, months) {
  const r = annualRate / (12 * 100);
  const emi = (principal * r * Math.pow(1 + r, months)) / (Math.pow(1 + r, months) - 1);
  return Number(emi.toFixed(2));
}

// 🔹 Route: Just calculate EMI
router.post("/calculate", authMiddleware, (req, res) => {
  const { principal, annualRate, months } = req.body;

  if (principal <= 0 || annualRate <= 0 || months <= 0) {
    return res.status(400).json({ success: false, message: "Invalid input" });
  }

  const emi = calculateEmi(principal, annualRate, months);

  res.json({
    success: true,
    data: {
      emi,
      totalPayable: Number((emi * months).toFixed(2)),
      totalInterest: Number((emi * months - principal).toFixed(2)),
    },
  });
});

// 🔹 Route: Check EMI against budget
router.post("/check", authMiddleware, async (req, res) => {
  const { principal, annualRate, months, category = "Overall" } = req.body;

  if (principal <= 0 || annualRate <= 0 || months <= 0) {
    return res.status(400).json({ success: false, message: "Invalid input" });
  }

  const principalPaise = BigInt(Math.round(principal * 100));

  const budgetImpact = await checkEmIAffordability({
    userId: req.user.userId,
    principalPaise,
    annualRate,
    months,
    category,
  });

  const emi = calculateEmi(principal, annualRate, months);

  res.json({
    success: true,
    data: {
      emi,
      totalPayable: Number((emi * months).toFixed(2)),
      totalInterest: Number((emi * months - principal).toFixed(2)),
      budgetImpact,
    },
  });
});

// 🔹 Route: Create EMI as recurring transaction
router.post("/create", authMiddleware, async (req, res) => {
  const { principal, annualRate, months, category = "EMI", name = "Loan EMI" } = req.body;

  if (principal <= 0 || annualRate <= 0 || months <= 0) {
    return res.status(400).json({ success: false, message: "Invalid input" });
  }

  const principalPaise = BigInt(Math.round(principal * 100));

  const budgetImpact = await checkEmIAffordability({
    userId: req.user.userId,
    principalPaise,
    annualRate,
    months,
    category,
  });

  if (!budgetImpact.affordable) {
    return res.status(400).json({
      success: false,
      message: "EMI exceeds your budget",
      data: budgetImpact,
    });
  }

  const emi = calculateEmi(principal, annualRate, months);

  const emiTransaction = new Transaction({
    name,
    price: -emi, // expense
    description: `EMI for loan (${months} months @ ${annualRate}%)`,
    datetime: new Date(),
    category,
    userId: req.user.userId,
    isRecurring: true,
    recurringFrequency: "Monthly",
    lastGeneratedAt: null,
    emiMeta: {
      principal,
      annualRate,
      tenureMonths: months,
      originalTenure: months,
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
