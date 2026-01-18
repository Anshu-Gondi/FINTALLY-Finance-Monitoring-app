const { Schema, model } = require("mongoose");

const TransactionSchema = new Schema({
  name: { type: String, required: true },
  price: { type: Number, required: true },
  description: { type: String, required: true },
  datetime: { type: Date, required: true },
  userId: { type: Schema.Types.ObjectId, ref: "User", required: true },

  // 🔄 Recurring Support
  isRecurring: { type: Boolean, default: false },
  recurringFrequency: {
    type: String,
    enum: ["Daily", "Weekly", "Monthly"],
    required: function () {
      return !!this.isRecurring;
    },
  },
  lastGeneratedAt: { type: Date, default: null },

  // 🏷️ Category Support
  category: { type: String, default: "General" },

  // 💳 EMI Metadata (optional, only for EMI transactions)
  emiMeta: {
    principal: { type: Number },
    annualRate: { type: Number },
    tenureMonths: { type: Number },
    originalTenure: { type: Number },
  }
});

const TransactionModel = model("Transaction", TransactionSchema);

module.exports = TransactionModel;
