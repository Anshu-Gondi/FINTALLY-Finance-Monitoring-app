const cron = require("node-cron");
const Transaction = require("../models/Transaction");

function getWeekKey(date) {
  const d = new Date(date);
  const oneJan = new Date(d.getFullYear(), 0, 1);
  const week = Math.ceil(((d - oneJan) / 86400000 + oneJan.getDay() + 1) / 7);
  return `${d.getFullYear()}-W${week}`;
}

function isNewPeriod(frequency, lastGeneratedAt, now) {
  if (!lastGeneratedAt) return true;

  if (frequency === "daily") {
    return (
      lastGeneratedAt.toISOString().split("T")[0] !==
      now.toISOString().split("T")[0]
    );
  }

  if (frequency === "weekly") {
    return getWeekKey(lastGeneratedAt) !== getWeekKey(now);
  }

  if (frequency === "monthly") {
    const lastMonth = `${lastGeneratedAt.getFullYear()}-${lastGeneratedAt.getMonth()}`;
    const thisMonth = `${now.getFullYear()}-${now.getMonth()}`;
    return lastMonth !== thisMonth;
  }

  return false;
}

const generateRecurringTransactions = async () => {
  try {
    const recurringTxns = await Transaction.find({
      isRecurring: true,
      recurringFrequency: { $ne: null },
    });

    const now = new Date();

    for (const tx of recurringTxns) {
      const frequency = tx.recurringFrequency?.toLowerCase();
      const lastGenerated = tx.lastGeneratedAt || tx.datetime;

      // 🛑 Auto-stop EMI if tenure completed
      if (
        tx.emiMeta &&
        tx.recurringFrequency?.toLowerCase() === "monthly" &&
        typeof tx.emiMeta.tenureMonths === "number" &&
        tx.emiMeta.tenureMonths <= 0
      ) {
        tx.isRecurring = false;
        tx.recurringFrequency = null;
        await tx.save();

        console.log(`🛑 EMI completed for user ${tx.userId}`);
        continue;
      }

      if (!frequency) continue;

      const shouldGenerate = isNewPeriod(frequency, lastGenerated, now);

      if (shouldGenerate) {
        const newTx = new Transaction({
          name: tx.name,
          price: tx.price,
          description: tx.description,
          datetime: now,
          category: tx.category,
          isRecurring: false,
          recurringFrequency: null,
          userId: tx.userId,
          lastGeneratedAt: null,
        });

        await newTx.save();

        // ⬇️ EMI tenure decrement
        if (
          tx.emiMeta &&
          tx.recurringFrequency?.toLowerCase() === "monthly" &&
          typeof tx.emiMeta.tenureMonths === "number"
        ) {
          tx.emiMeta.tenureMonths -= 1;
        }

        tx.lastGeneratedAt = now;

        // 🛑 Auto-disable EMI if finished
        if (tx.emiMeta && tx.emiMeta.tenureMonths === 0) {
          tx.isRecurring = false;
          tx.recurringFrequency = null;
        }

        if (tx.emiMeta?.tenureMonths < 0) {
          tx.emiMeta.tenureMonths = 0;
        }

        await tx.save();

        console.log(
          `✅ Recurring ${frequency} transaction added for user ${tx.userId}`,
        );
      }
    }
  } catch (error) {
    console.error(
      "❌ Error in recurring transactions cron job:",
      error.message,
    );
  }
};

// Run every minute
cron.schedule("* * * * *", () => {
  generateRecurringTransactions();
});

module.exports = generateRecurringTransactions;
