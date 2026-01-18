const express = require("express");
const router = express.Router();
const authMiddleware = require("../middlewares/auth");

const TaxProfile = require("../models/TaxProfiles");
const {
  calculateSlabTax,
  calculateFlatTax,
} = require("../services/tax.service");
const { getCountryTax } = require("../services/taxApi.service");
const { getAnnualIncome } = require("../services/income.service");
const defaultIndiaTax = require("../config/defaultTaxIndia");

router.get("/calculate", authMiddleware, async (req, res) => {
  const userId = req.user.userId;
  const year = Number(req.query.year) || new Date().getFullYear();

  // ✅ Fetch once
  const taxProfile = await TaxProfile.findOne({ userId });

  // ✅ Country priority: Profile → Query → Default
  const countryCode = (
    taxProfile?.countryCode ||
    req.query.country ||
    "IN"
  ).toUpperCase();

  const income = await getAnnualIncome(userId, year);

  // 🇮🇳 INDIA → Slab Tax
  if (countryCode === "IN") {
    const slabs = taxProfile?.slabs || defaultIndiaTax.slabs;
    const cess = taxProfile?.cess ?? defaultIndiaTax.cess;

    const taxResult = calculateSlabTax(income, slabs, cess);

    return res.json({
      success: true,
      data: {
        country: "IN",
        year,
        income,
        ...taxResult,
        source: taxProfile ? taxProfile.source : "DEFAULT"
      }
    });
  }

  // 🌍 WORLD TAX → Flat Tax
  const apiTax = getCountryTax(countryCode);

  if (!apiTax) {
    return res.status(404).json({
      success: false,
      message: "Tax data not available for this country"
    });
  }

  const taxResult = calculateFlatTax(income, apiTax.rate);

  res.json({
    success: true,
    data: {
      country: countryCode,
      year,
      income,
      taxType: apiTax.type,
      rate: apiTax.rate,
      ...taxResult,
      source: "API"
    }
  });
});

module.exports = router;