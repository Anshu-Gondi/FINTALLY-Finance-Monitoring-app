import PropTypes from "prop-types";
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from "recharts";

export default function TrendChartBlock({ data, loading }) {
  if (loading) return <p>Loading…</p>;
  if (!data || !data.length) return <p>No trend data available.</p>;

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
  data: PropTypes.arrayOf(PropTypes.object),
  loading: PropTypes.bool,
};

TrendChartBlock.defaultProps = {
  data: [],
  loading: false,
};