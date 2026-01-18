function normalizeAmount(value) {
  return Number(value) || 0;
}

function isExpense(value) {
  return normalizeAmount(value) < 0;
}

function isIncome(value) {
  return normalizeAmount(value) > 0;
}

function expenseValue(value) {
  return Math.abs(Math.min(normalizeAmount(value), 0));
}

function incomeValue(value) {
  return Math.max(normalizeAmount(value), 0);
}

module.exports = {
  isExpense,
  isIncome,
  expenseValue,
  incomeValue
};
