function calculateSlabTax(income, slabs, cess = 0) {
  let tax = 0;
  let remaining = income;

  for (const slab of slabs) {
    if (remaining <= 0) break;

    const slabRange = slab.upto - slab.from;
    const taxable = Math.min(remaining, slabRange);

    tax += taxable * slab.rate;
    remaining -= taxable;
  }

  const cessAmount = tax * cess;

  return {
    baseTax: Number(tax.toFixed(2)),
    cess: Number(cessAmount.toFixed(2)),
    totalTax: Number((tax + cessAmount).toFixed(2))
  };
}

function calculateFlatTax(income, rate) {
  const normalizedRate = rate > 1 ? rate / 100 : rate;
  const tax = income * normalizedRate;

  return {
    baseTax: Number(tax.toFixed(2)),
    cess: 0,
    totalTax: Number(tax.toFixed(2))
  };
}

module.exports = {
  calculateSlabTax,
  calculateFlatTax
};