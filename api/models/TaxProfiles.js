const { Schema, model } = require("mongoose");

const TaxProfileSchema = new Schema(
  {
    userId: { type: Schema.Types.ObjectId, ref: "User", unique: true },

    slabs: [
      {
        from: Number,
        upto: Number,
        rate: Number,
      },
    ],

    cess: { type: Number, default: 0.04 },

    source: {
      type: String,
      enum: ["API", "CUSTOM", "DEFAULT"],
      default: "API",
    },

    countryCode: {
      type: String,
      default: "IN",
    },
  },
  { timestamps: true } // ✅ auto createdAt + updatedAt
);

module.exports = model("TaxProfile", TaxProfileSchema);
