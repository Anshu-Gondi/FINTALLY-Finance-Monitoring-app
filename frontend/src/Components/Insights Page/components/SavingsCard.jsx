// SavingsCard.jsx — uses actual savings-optimization endpoint shape
import { useState, useEffect } from "react";
import { analyticsApi } from "../../../services/api";

export default function SavingsCard() {
  const [data, setData] = useState(null);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    setLoading(true);
    analyticsApi.savingsOptimization()
      .then(res => setData(res))
      .catch(() => setData(null))
      .finally(() => setLoading(false));
  }, []);

  if (loading) return <p>Loading savings…</p>;
  if (!data) return <p>No savings data available.</p>;

  return (
    <div className="box">
      <p>
        <strong>Saving Rate:</strong>{" "}
        {(data.saving_rate_percent ?? 0).toFixed(1)}%
      </p>
      <p>
        <strong>Financial Health Score:</strong>{" "}
        {(data.financial_health_score ?? 0).toFixed(0)} / 100
      </p>
    </div>
  );
}