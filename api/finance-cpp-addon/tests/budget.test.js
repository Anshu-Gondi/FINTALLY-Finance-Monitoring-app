const path = require("path");

const native = require(
  path.join(__dirname, "..", "build", "Release", "finance.node"),
);

describe("budgetProjectionBatch", () => {
  test("SAFE budget", () => {
    const res = native.budgetProjectionBatch(
      new BigInt64Array([5000n]), // EMI
      new BigInt64Array([100000n]), // Budget
      new BigInt64Array([10000n]), // Spent
      new Int32Array([10]), // Months
    );

    expect(res.warningFlag[0]).toBe(0);
  });

  test("NEAR_LIMIT budget", () => {
    const res = native.budgetProjectionBatch(
      new BigInt64Array([7000n]), // EMI
      new BigInt64Array([100000n]),
      new BigInt64Array([20000n]),
      new Int32Array([10]),
    );

    // 7000*10 + 20000 = 90000 → 90%
    expect(res.warningFlag[0]).toBe(1);
  });

  test("EXCEEDED budget", () => {
    const res = native.budgetProjectionBatch(
      new BigInt64Array([12000n]),
      new BigInt64Array([100000n]),
      new BigInt64Array([30000n]),
      new Int32Array([10]),
    );

    expect(res.warningFlag[0]).toBe(2);
  });
});
