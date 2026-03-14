import PropTypes from "prop-types";
import { PieChart, Pie, Cell, Tooltip, Legend, ResponsiveContainer } from "recharts";

const COLORS = ["#00d1b2", "#ff3860", "#ffdd57", "#3273dc", "#b86bff", "#ff8c00"];

export default function PieChartBlock({ data }) {
  if (!data || !data.length) return <p>No category data.</p>;

  return (
    <ResponsiveContainer width="100%" height={300}>
      <PieChart>
        <Pie data={data} dataKey="total" nameKey="category" cx="50%" cy="50%" outerRadius={100} label>
          {data.map((entry, index) => (
            <Cell key={`cell-${index}`} fill={COLORS[index % COLORS.length]} />
          ))}
        </Pie>
        <Tooltip />
        <Legend />
      </PieChart>
    </ResponsiveContainer>
  );
}

PieChartBlock.propTypes = {
  data: PropTypes.arrayOf(PropTypes.object),
};

PieChartBlock.defaultProps = {
  data: [],
};