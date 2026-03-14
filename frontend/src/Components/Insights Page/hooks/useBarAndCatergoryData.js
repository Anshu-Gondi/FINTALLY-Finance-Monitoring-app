import { useState, useEffect } from "react";

const BAR_URLS = {
  daily: "/insights/daily",
  weekly: "/insights/period?range=weekly",
  monthly: "/insights/period?range=monthly",
  lifetime: "/insights/lifetime",
  max: "/insights/min-max-transaction",
  min: "/insights/min-max-transaction",
};

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
        const barUrl = BAR_URLS[mode] || "/insights/period?range=monthly";

        const catParams = new URLSearchParams({
          start: startDate?.toISOString(),
          end: endDate?.toISOString(),
          type,
          keyword,
        });

        const [barRes, catRes] = await Promise.all([
          fetch(`${import.meta.env.VITE_API_URL}${barUrl}`, { headers: { Authorization: `Bearer ${token}` } }).then(r => r.json()),
          fetch(`${import.meta.env.VITE_API_URL}/insights/categories?${catParams}`, { headers: { Authorization: `Bearer ${token}` } }).then(r => r.json()),
        ]);

        let bars = barRes.data || [];
        if (mode === "max") bars = bars.filter(x => x.total > 0);
        if (mode === "min") bars = bars.filter(x => x.total < 0);

        setBarData(bars);
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