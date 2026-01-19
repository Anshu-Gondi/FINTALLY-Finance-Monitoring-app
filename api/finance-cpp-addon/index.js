const path = require("path");

// Load native addon
const native = require(
  path.join(__dirname, "build", "Release", "finance.node"),
);

/**
 * JS-safe wrapper
 * (keeps TypedArray handling isolated)
 */
function calculateEmi({ principalPaise, annualRate, months }) {
  const result = native.emiBatch(
    new BigInt64Array([BigInt(principalPaise)]),
    new Float64Array([annualRate]),
    new Int32Array([months]),
  );

  return Number(result[0]); // return paise
}

function calculateBudgetBatch({ emis, budgets, spentSoFar, months }) {
  const result = native.calculateBudgetProjectionBatch(
    new BigInt64Array(emis.map(BigInt)),
    new BigInt64Array(budgets.map(BigInt)),
    new BigInt64Array(spentSoFar.map(BigInt)),
    new Int32Array(months),
  );

  return {
    projectedSpent: Array.from(result.projectedSpent, (v) => Number(v)),
    usagePercent: Array.from(result.usagePercent),
    warningFlag: Array.from(result.warningFlag),
  };
}

module.exports = {
  calculateEmi,
  calculateBudgetBatch,
};
