import PropTypes from "prop-types";
import {
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
} from "recharts";

// Currency formatter for Y-Axis
const formatYAxis = (val) => {
  if (Math.abs(val) >= 100000) return `₹${(val / 100000).toFixed(1)}L`;
  if (Math.abs(val) >= 1000) return `₹${(val / 1000).toFixed(0)}k`;
  return `₹${val}`;
};

// Custom Tooltip Component
const CustomTooltip = ({ active, payload, label }) => {
  if (active && payload && payload.length) {
    return (
      <div className="box p-3 shadow-sm border" style={{ minWidth: "170px" }}>
        <p className="is-size-7 has-text-weight-bold has-text-grey mb-2">
          Period: {label || "—"}
        </p>

        {payload.map((item, index) => {
          const isIncome = item.dataKey === "income";
          const isExpense = item.dataKey === "expense";
          const val = item.value ?? 0;

          return (
            <div
              key={index}
              className="is-flex is-justify-content-space-between is-align-items-center mb-1"
            >
              <span className="is-size-7" style={{ color: item.color }}>
                ● {item.name}:
              </span>
              <span className="is-size-7 is-family-monospace has-text-weight-bold ml-2">
                {isIncome ? "+" : isExpense ? "-" : ""}₹
                {Math.abs(val).toLocaleString("en-IN", {
                  maximumFractionDigits: 0,
                })}
              </span>
            </div>
          );
        })}
      </div>
    );
  }
  return null;
};

CustomTooltip.propTypes = {
  active: PropTypes.bool,
  payload: PropTypes.array,
  label: PropTypes.string,
};

export default function BarChartBlock({ data, loading }) {
  // Skeleton Loading State
  if (loading) {
    return (
      <div className="box p-5">
        <div className="is-flex is-justify-content-space-between mb-4">
          <div
            style={{
              width: "40%",
              height: "24px",
              background: "#eee",
              borderRadius: "4px",
            }}
          />
          <div
            style={{
              width: "20%",
              height: "24px",
              background: "#eee",
              borderRadius: "4px",
            }}
          />
        </div>
        <div
          style={{
            width: "100%",
            height: "250px",
            background: "#f8f8f8",
            borderRadius: "6px",
          }}
        />
      </div>
    );
  }

  // Empty State
  if (!data || !data.length) {
    return (
      <div className="box p-5 has-text-centered">
        <span className="icon is-large has-text-grey-light mb-2">
          <i className="fas fa-chart-bar fa-2x" />
        </span>
        <p className="has-text-grey">No comparative summary data available.</p>
      </div>
    );
  }

  // Detect available keys in data to avoid rendering empty bars
  const hasIncome = data.some((item) => item.income !== undefined && item.income !== null);
  const hasExpense = data.some((item) => item.expense !== undefined && item.expense !== null);
  const hasTotalOnly = !hasIncome && !hasExpense && data.some((item) => item.total !== undefined);

  return (
    <div className="box p-4">
      {/* Header */}
      <div className="level is-mobile mb-4">
        <div className="level-left">
          <div>
            <h3 className="title is-5 mb-1">
              <span className="mr-2">📊</span> Transaction Volume Comparison
            </h3>
            <p className="subtitle is-7 has-text-grey">
              Aggregate balance and volume across selected view mode
            </p>
          </div>
        </div>
      </div>

      {/* Chart Canvas */}
      <div style={{ width: "100%", height: 310 }}>
        <ResponsiveContainer width="100%" height="100%">
          <BarChart data={data} margin={{ top: 10, right: 10, left: -10, bottom: 0 }}>
            <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#f0f0f0" />
            <XAxis
              dataKey="period"
              tick={{ fontSize: 12, fill: "#7a7a7a" }}
              tickLine={false}
              axisLine={{ stroke: "#dbdbdb" }}
            />
            <YAxis
              tickFormatter={formatYAxis}
              tick={{ fontSize: 12, fill: "#7a7a7a" }}
              tickLine={false}
              axisLine={false}
            />
            <Tooltip content={<CustomTooltip />} />
            <Legend verticalAlign="top" height={36} align="right" iconType="circle" />

            {/* Conditional Bars */}
            {hasIncome && (
              <Bar
                dataKey="income"
                fill="#48c774"
                name="Income"
                radius={[4, 4, 0, 0]}
                maxBarSize={40}
              />
            )}
            {hasExpense && (
              <Bar
                dataKey="expense"
                fill="#ff3860"
                name="Expense"
                radius={[4, 4, 0, 0]}
                maxBarSize={40}
              />
            )}
            {(hasTotalOnly || (!hasIncome && !hasExpense)) && (
              <Bar
                dataKey="total"
                fill="#3273dc"
                name="Total Volume"
                radius={[4, 4, 0, 0]}
                maxBarSize={40}
              />
            )}
          </BarChart>
        </ResponsiveContainer>
      </div>
    </div>
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
