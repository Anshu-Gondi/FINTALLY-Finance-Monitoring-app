const path = require("path");

const native = require(
  path.join(__dirname, "..", "build", "Release", "finance.node")
);

describe("emiBatch", () => {
  test("calculates EMI correctly", () => {
    const result = native.emiBatch(
      new BigInt64Array([10000000n]), // ₹1,00,000 in paise
      new Float64Array([12]),         // 12%
      new Int32Array([12])            // 12 months
    );

    expect(result.length).toBe(1);
    expect(typeof result[0]).toBe("bigint");
    expect(result[0]).toBeGreaterThan(0n);
  });

  test("zero interest EMI", () => {
    const result = native.emiBatch(
      new BigInt64Array([120000n]),
      new Float64Array([0]),
      new Int32Array([12])
    );

    expect(result[0]).toBe(10000n);
  });

  test("invalid months returns zero", () => {
    const result = native.emiBatch(
      new BigInt64Array([100000n]),
      new Float64Array([10]),
      new Int32Array([0])
    );

    expect(result[0]).toBe(0n);
  });
});
