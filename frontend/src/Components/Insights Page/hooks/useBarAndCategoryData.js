import { useState, useEffect } from "react";
import { analyticsApi } from "../../../services/api";

export default function useBarAndCategoryData({
  mode,
  startDate,
  endDate,
  type,
  keyword,
  token,
}) {
  const [barData, setBarData] = useState([]);
  const [categoryData, setCategoryData] = useState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);

  useEffect(() => {
    // Skip if user is unauthenticated
    if (!token) return;

    let isSubscribed = true;
    setLoading(true);
    setError(null);

    const fetchData = async () => {
      try {
        // ── 1. Fetch Bar Data ──────────────────────────────────────────
        let barRes;
        if (mode === "daily") {
          barRes = await analyticsApi.dailySummary();
        } else if (mode === "lifetime") {
          barRes = await analyticsApi.lifetimeAnalysis();
        } else if (mode === "max" || mode === "min") {
          barRes = await analyticsApi.minMaxTransaction();
        } else {
          // Default: "weekly" | "monthly"
          barRes = await analyticsApi.periodSummary(mode);
        }

        let bars = barRes?.data ?? [];

        // Apply local filtering for min/max modes if needed
        if (mode === "max") bars = bars.filter((x) => (x.total ?? 0) > 0);
        if (mode === "min") bars = bars.filter((x) => (x.total ?? 0) < 0);

        // ── 2. Fetch Category Data ──────────────────────────────────────
        const catRes = await analyticsApi.categorySummary({
          start: startDate ? new Date(startDate).toISOString().split("T")[0] : undefined,
          end: endDate ? new Date(endDate).toISOString().split("T")[0] : undefined,
          type: type !== "all" ? type : undefined,
          keyword: keyword?.trim() || undefined,
        });

        if (isSubscribed) {
          setBarData(bars);
          setCategoryData(catRes?.data ?? []);
        }
      } catch (err) {
        if (isSubscribed) {
          console.error("Failed to fetch analytics bar & category data:", err);
          setError(err?.message || "Error loading dashboard metrics.");
          setBarData([]);
          setCategoryData([]);
        }
      } finally {
        if (isSubscribed) {
          setLoading(false);
        }
      }
    };

    fetchData();

    // Cleanup switch for rapid filter adjustments
    return () => {
      isSubscribed = false;
    };
  }, [mode, startDate, endDate, type, keyword, token]);

  return { barData, categoryData, loading, error };
}
