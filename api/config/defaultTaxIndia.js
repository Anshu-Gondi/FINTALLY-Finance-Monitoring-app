module.exports = {
  regime: "old",
  slabs: [
    { from: 0, upto: 250000, rate: 0.0 },
    { from: 250000, upto: 500000, rate: 0.05 },
    { from: 500000, upto: 1000000, rate: 0.20 },
    { from: 1000000, upto: Infinity, rate: 0.30 }
  ],
  cess: 0.04 // 4% health & education cess
};
