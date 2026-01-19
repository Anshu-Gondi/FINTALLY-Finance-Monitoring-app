const cron = require("node-cron");
const Transaction = require("../models/Transaction");

// Helper to get week key
function getWeekKey(date) {
  const d = new Date(date);
  const oneJan = new Date(d.getFullYear(), 0, 1);
  const week = Math.ceil(((d - oneJan) / 86400000 + oneJan.getDay() + 1) / 7);
  return `${d.getFullYear()}-W${week}`;
}

// Check if a new period has started
function isNewPeriod(frequency, lastGeneratedAt, now) {
  if (!lastGeneratedAt) return true;

  if (frequency === "daily") {
    return lastGeneratedAt.toISOString().split("T")[0] !== now.toISOString().split("T")[0];
  }

  if (frequency === "weekly") {
    return getWeekKey(lastGeneratedAt) !== getWeekKey(now);
  }

  if (frequency === "monthly") {
    return lastGeneratedAt.getFullYear() !== now.getFullYear() || lastGeneratedAt.getMonth() !== now.getMonth();
  }

  return false;
}

// 🔹 Optimized Recurring Transaction Generator
const generateRecurringTransactions = async () => {
  try {
    const now = new Date();

    // Only fetch transactions that are recurring AND are active (not finished)
    const recurringTxns = await Transaction.find({
      isRecurring: true,
      recurringFrequency: { $ne: null },
      $or: [
        { "emiMeta.tenureMonths": { $gt: 0 } },
        { emiMeta: { $exists: false } } // Non-EMI recurring transactions
      ]
    }).lean(); // Use lean for faster read-only query

    if (!recurringTxns.length) return;

    // Batch insert new transactions instead of one by one
    const toInsert = [];
    const toUpdate = [];

    for (const tx of recurringTxns) {
      const frequency = tx.recurringFrequency?.toLowerCase();
      const lastGenerated = tx.lastGeneratedAt || tx.datetime;

      if (!frequency) continue;

      const shouldGenerate = isNewPeriod(frequency, lastGenerated, now);

      if (!shouldGenerate) continue;

      // Create new transaction object
      const newTx = {
        name: tx.name,
        price: tx.price,
        description: tx.description,
        datetime: now,
        category: tx.category,
        userId: tx.userId,
        isRecurring: false,
        recurringFrequency: null,
        lastGeneratedAt: null,
      };
      toInsert.push(newTx);

      // Update original recurring transaction
      const updatedTx = { _id: tx._id, lastGeneratedAt: now };

      if (tx.emiMeta?.tenureMonths != null) {
        updatedTx["emiMeta.tenureMonths"] = Math.max(tx.emiMeta.tenureMonths - 1, 0);
        if (updatedTx["emiMeta.tenureMonths"] === 0) {
          updatedTx.isRecurring = false;
          updatedTx.recurringFrequency = null;
        }
      }

      toUpdate.push(updatedTx);
    }

    // 🔹 Bulk insert
    if (toInsert.length) {
      await Transaction.insertMany(toInsert);
      console.log(`✅ ${toInsert.length} recurring transactions created`);
    }

    // 🔹 Bulk update
    for (const tx of toUpdate) {
      await Transaction.updateOne({ _id: tx._id }, { $set: tx });
    }

  } catch (error) {
    console.error("❌ Error in recurring transactions cron job:", error.message);
  }
};

// Run every 5 minutes (or choose your frequency)
cron.schedule("*/5 * * * *", () => {
  generateRecurringTransactions();
});

module.exports = generateRecurringTransactions;
