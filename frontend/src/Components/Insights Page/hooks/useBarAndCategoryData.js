// useBarAndCategoryData.js — rewritten to use correct endpoints via analyticsApi
import { useState, useEffect } from "react";
import { analyticsApi } from "../../../services/api";

export default function useBarAndCategoryData({ mode, startDate, endDate, type, keyword, token }) {
  const [barData, setBarData]       = useState([]);
  const [categoryData, setCategoryData] = useState([]);
  const [loading, setLoading]       = useState(false);
  const [error, setError]           = useState(null);

  useEffect(() => {
    if (!token) return;
    setLoading(true);
    setError(null);

    const fetchData = async () => {
      try {
        // ── Bar data ──────────────────────────────────────────────
        let barRes;
        if (mode === "daily") {
          barRes = await analyticsApi.dailySummary();
        } else if (mode === "lifetime") {
          barRes = await analyticsApi.lifetimeAnalysis();
        } else if (mode === "max" || mode === "min") {
          barRes = await analyticsApi.minMaxTransaction();
        } else {
          // "weekly" | "monthly"
          barRes = await analyticsApi.periodSummary(mode);
        }

        let bars = barRes?.data ?? [];
        if (mode === "max") bars = bars.filter(x => x.total > 0);
        if (mode === "min") bars = bars.filter(x => x.total < 0);

        // ── Category data ─────────────────────────────────────────
        const catRes = await analyticsApi.categorySummary({
          start:   startDate ? new Date(startDate).toISOString().split("T")[0] : undefined,
          end:     endDate   ? new Date(endDate).toISOString().split("T")[0]   : undefined,
          type:    type !== "all" ? type : undefined,
          keyword: keyword || undefined,
        });

        setBarData(bars);
        setCategoryData(catRes?.data ?? []);
      } catch (err) {
        setError(err.message);
      } finally {
        setLoading(false);
      }
    };

    fetchData();
  }, [mode, startDate, endDate, type, keyword, token]);

  return { barData, categoryData, loading, error };
}