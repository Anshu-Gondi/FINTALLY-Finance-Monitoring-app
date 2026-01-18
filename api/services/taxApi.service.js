const taxData = require("../data/WorldTaxRates.json");

function getCountryTax(countryCode) {
  return taxData[countryCode.toUpperCase()] || null;
}

module.exports = { getCountryTax };
