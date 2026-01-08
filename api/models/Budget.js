const { Schema, model } = require("mongoose");

const BudgetSchema = new Schema({
  userId: { type: Schema.Types.ObjectId, ref: "User", required: true },

  // Optional: Allow budgets per category or overall
  category: { type: String, default: "Overall" },

  amount: { type: Number, required: true }, // Budget limit
  spent: { type: Number, default: 0 },      // Auto-updated based on transactions

  startDate: { type: Date, default: Date.now }, // Start period
  endDate: { type: Date }, // End of budget period

  // Optional: allow repeating monthly budgets
  isRecurring: { type: Boolean, default: false },

  createdAt: { type: Date, default: Date.now },
});

const Budget = model("Budget", BudgetSchema);
module.exports = Budget;
