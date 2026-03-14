import PropTypes from "prop-types";
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from "recharts";

export default function CashflowChartBlock({ data }) {
  if (!data || !data.length) return <p>Forecast unavailable</p>;

  return (
    <ResponsiveContainer width="100%" height={300}>
      <LineChart data={data}>
        <CartesianGrid strokeDasharray="3 3" />
        <XAxis dataKey="horizonDays" />
        <YAxis />
        <Tooltip />
        <Legend />
        <Line type="monotone" dataKey="expectedBalance" stroke="#3273dc" name="Expected Balance" />
      </LineChart>
    </ResponsiveContainer>
  );
}

CashflowChartBlock.propTypes = {
  data: PropTypes.arrayOf(PropTypes.object),
};

CashflowChartBlock.defaultProps = {
  data: [],
};