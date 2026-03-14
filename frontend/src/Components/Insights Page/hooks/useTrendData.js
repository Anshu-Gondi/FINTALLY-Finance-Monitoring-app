import { useState, useEffect } from "react";

export default function useBarAndCategoryData({ mode, startDate, endDate, type, keyword, token }) {
  const [barData, setBarData] = useState([]);
  const [categoryData, setCategoryData] = useState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);

  useEffect(() => {
    if (!token) return;
    setLoading(true);
    setError(null);

    const fetchData = async () => {
      try {
        let barUrl = `/insights/period?range=${mode}`;
        if (mode === "daily") barUrl = "/insights/daily";
        if (mode === "lifetime") barUrl = "/insights/lifetime";
        if (mode === "max" || mode === "min") barUrl = "/insights/min-max-transaction";

        const barRes = await fetch(`${import.meta.env.VITE_API_URL}${barUrl}`, {
          headers: { Authorization: `Bearer ${token}` },
        }).then(r => r.json());

        const catParams = new URLSearchParams({
          start: startDate?.toISOString(),
          end: endDate?.toISOString(),
          type,
          keyword,
        });
        const catRes = await fetch(`${import.meta.env.VITE_API_URL}/insights/categories?${catParams}`, {
          headers: { Authorization: `Bearer ${token}` },
        }).then(r => r.json());

        let bars = [];
        if (mode === "daily") bars = barRes.data;
        if (mode === "weekly" || mode === "monthly") bars = barRes.data;
        if (mode === "lifetime") bars = barRes.data;
        if (mode === "max") bars = barRes.data.filter(x => x.total > 0);
        if (mode === "min") bars = barRes.data.filter(x => x.total < 0);

        setBarData(bars || []);
        setCategoryData(catRes.data || []);
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