const Transaction = require("../models/Transaction");

async function getAnnualIncome(userId, year) {
  const start = new Date(`${year}-01-01`);
  const end = new Date(`${year}-12-31`);

  const result = await Transaction.aggregate([
    {
      $match: {
        userId,
        datetime: { $gte: start, $lte: end },
        price: { $gt: 0 } // ✅ income only
      }
    },
    {
      $group: {
        _id: null,
        income: { $sum: "$price" }
      }
    }
  ]);

  return result[0]?.income || 0;
}

module.exports = { getAnnualIncome };
