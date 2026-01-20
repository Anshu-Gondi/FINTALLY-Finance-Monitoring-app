const path = require("path");

const native = require(
  path.join(__dirname, "..", "build", "Release", "finance.node")
);

describe("compoundInterestBatch", () => {
  test("calculates compound interest", () => {
    const result = native.compoundInterestBatch(
      new BigInt64Array([100000n]),
      new Float64Array([10]),
      new Int32Array([2]),
      new Int32Array([1])
    );

    expect(result[0]).toBeGreaterThan(100000n);
  });
});
