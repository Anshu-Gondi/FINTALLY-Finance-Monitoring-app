import PropTypes from "prop-types";
import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from "recharts";

export default function BarChartBlock({ data, loading }) {
  if (loading) return <p>Loading…</p>;
  if (!data || !data.length) return <p>No data available.</p>;

  return (
    <ResponsiveContainer width="100%" height={300}>
      <BarChart data={data}>
        <CartesianGrid strokeDasharray="3 3" />
        <XAxis dataKey="period" />
        <YAxis />
        <Tooltip />
        <Legend />
        <Bar dataKey="total" fill="#00d1b2" name="Total" />
        <Bar dataKey="income" fill="#48c774" name="Income" />
        <Bar dataKey="expense" fill="#ff3860" name="Expense" />
      </BarChart>
    </ResponsiveContainer>
  );
}

BarChartBlock.propTypes = {
  data: PropTypes.arrayOf(PropTypes.object),
  loading: PropTypes.bool,
};

BarChartBlock.defaultProps = {
  data: [],
  loading: false,
};