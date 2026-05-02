import PropTypes from "prop-types";
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from "recharts";

// CashflowChartBlock.jsx — API returns points: [{horizon_days, expected_balance}]
// Hook sets cashflowForecast = cashflow.value which is {points: [...]}
// So data here is the full response object, not the array
export default function CashflowChartBlock({ data }) {
  const points = data?.points ?? [];
  if (!points.length) return <p>Forecast unavailable</p>;

  return (
    <ResponsiveContainer width="100%" height={300}>
      <LineChart data={points}>
        <CartesianGrid strokeDasharray="3 3" />
        <XAxis dataKey="horizon_days" />
        <YAxis />
        <Tooltip />
        <Legend />
        <Line
          type="monotone"
          dataKey="expected_balance"
          stroke="#3273dc"
          name="Expected Balance"
        />
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