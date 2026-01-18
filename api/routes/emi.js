const express = require("express");
const router = express.Router();
const authMiddleware = require("../middlewares/auth");
const Transaction = require("../models/Transaction");
const { checkEmIAffordability } = require("../services/emiBudget.service");

router.post("/calculate", authMiddleware, (req, res) => {
  const { principal, annualRate, months } = req.body;

  if (principal <= 0 || annualRate <= 0 || months <= 0) {
    return res.status(400).json({ success: false, message: "Invalid input" });
  }

  const r = annualRate / (12 * 100);
  const emi =
    (principal * r * Math.pow(1 + r, months)) /
    (Math.pow(1 + r, months) - 1);

  res.json({
    success: true,
    data: {
      emi: Number(emi.toFixed(2)),
      totalPayable: Number((emi * months).toFixed(2)),
      totalInterest: Number((emi * months - principal).toFixed(2)),
    },
  });
});

// 🧮 EMI + Budget Check
router.post("/check", authMiddleware, async (req, res) => {
  const { principal, annualRate, months, category } = req.body;

  if (principal <= 0 || annualRate <= 0 || months <= 0) {
    return res.status(400).json({ success: false, message: "Invalid input" });
  }

  const r = annualRate / (12 * 100);
  const emi =
    (principal * r * Math.pow(1 + r, months)) /
    (Math.pow(1 + r, months) - 1);

  const emiAmount = Number(emi.toFixed(2));

  const budgetCheck = await checkEmIAffordability({
    userId: req.user.userId,
    emiAmount,
    category
  });

  res.json({
    success: true,
    data: {
      emi: emiAmount,
      totalPayable: Number((emiAmount * months).toFixed(2)),
      totalInterest: Number(
        (emiAmount * months - principal).toFixed(2)
      ),
      budgetImpact: budgetCheck
    }
  });
});

// ✅ Create EMI as recurring transaction
router.post("/create", authMiddleware, async (req, res) => {
  const {
    principal,
    annualRate,
    months,
    category = "EMI",
    name = "Loan EMI"
  } = req.body;

  if (principal <= 0 || annualRate <= 0 || months <= 0) {
    return res.status(400).json({ success: false, message: "Invalid input" });
  }

  // 1️⃣ Calculate EMI
  const r = annualRate / (12 * 100);
  const emi =
    (principal * r * Math.pow(1 + r, months)) /
    (Math.pow(1 + r, months) - 1);

  const emiAmount = Number(emi.toFixed(2));

  // 2️⃣ Budget affordability check
  const affordability = await checkEmIAffordability({
    userId: req.user.userId,
    emiAmount,
    category
  });

  if (!affordability.affordable) {
    return res.status(400).json({
      success: false,
      message: "EMI exceeds your budget",
      data: affordability
    });
  }

  // 3️⃣ Create recurring EMI transaction (MASTER)
  const emiTransaction = new Transaction({
    name,
    price: -emiAmount,                 // ❗ expense
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
      originalTenure: months
    }
  });

  await emiTransaction.save();

  res.json({
    success: true,
    message: "EMI added as recurring transaction",
    data: {
      emi: emiAmount,
      transactionId: emiTransaction._id,
      budgetImpact: affordability
    }
  });
});

module.exports = router;