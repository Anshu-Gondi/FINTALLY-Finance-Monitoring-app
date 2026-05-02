// TrendChartBlock.jsx — fetches its own data using the same pattern as useAdvancedAnalytics
import { useState, useEffect } from "react";
import PropTypes from "prop-types";
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from "recharts";
import { analyticsApi } from "../../../services/api";

export default function TrendChartBlock({ trendMode }) {
  const [data, setData] = useState([]);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    setLoading(true);
    analyticsApi.trendSummary(trendMode)
      .then(res => setData(res?.data ?? []))
      .catch(() => setData([]))
      .finally(() => setLoading(false));
  }, [trendMode]);

  if (loading) return <p>Loading trend…</p>;
  if (!data.length) return <p>No trend data available.</p>;

  return (
    <ResponsiveContainer width="100%" height={300}>
      <LineChart data={data}>
        <CartesianGrid strokeDasharray="3 3" />
        <XAxis dataKey="period" />
        <YAxis />
        <Tooltip />
        <Legend />
        <Line type="monotone" dataKey="income" stroke="#48c774" name="Income" />
        <Line type="monotone" dataKey="expense" stroke="#ff3860" name="Expense" />
      </LineChart>
    </ResponsiveContainer>
  );
}

TrendChartBlock.propTypes = {
  trendMode: PropTypes.string.isRequired,
};