import { useState, useEffect } from "react";
import PropTypes from "prop-types";
import {
  AreaChart,
  Area,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
} from "recharts";
import { analyticsApi } from "../../../services/api";

// Helper to format currency values cleanly
const formatCurrency = (val) => {
  if (val >= 100000) return `₹${(val / 100000).toFixed(1)}L`;
  if (val >= 1000) return `₹${(val / 1000).toFixed(0)}k`;
  return `₹${val}`;
};

// Custom Tooltip Component
const CustomTooltip = ({ active, payload, label }) => {
  if (active && payload && payload.length) {
    const income = payload.find((p) => p.dataKey === "income")?.value ?? 0;
    const expense = payload.find((p) => p.dataKey === "expense")?.value ?? 0;
    const net = income - expense;

    return (
      <div className="box p-3 shadow-sm border" style={{ minWidth: "180px" }}>
        <p className="is-size-7 has-text-weight-bold has-text-grey mb-2">
          Period: {label}
        </p>

        <div className="is-flex is-justify-content-space-between is-align-items-center mb-1">
          <span className="is-size-7 has-text-success-dark">
            <span className="mr-1">●</span> Income:
          </span>
          <span className="is-size-7 is-family-monospace has-text-weight-bold has-text-success-dark">
            +₹{income.toLocaleString("en-IN", { maximumFractionDigits: 0 })}
          </span>
        </div>

        <div className="is-flex is-justify-content-space-between is-align-items-center mb-1">
          <span className="is-size-7 has-text-danger-dark">
            <span className="mr-1">●</span> Expense:
          </span>
          <span className="is-size-7 is-family-monospace has-text-weight-bold has-text-danger-dark">
            -₹{expense.toLocaleString("en-IN", { maximumFractionDigits: 0 })}
          </span>
        </div>

        <hr className="my-2" />

        <div className="is-flex is-justify-content-space-between is-align-items-center">
          <span className="is-size-7 has-text-grey">Net:</span>
          <span
            className={`is-size-7 is-family-monospace has-text-weight-bold ${
              net >= 0 ? "has-text-success" : "has-text-danger"
            }`}
          >
            {net >= 0 ? "+" : "-"}₹
            {Math.abs(net).toLocaleString("en-IN", { maximumFractionDigits: 0 })}
          </span>
        </div>
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

export default function TrendChartBlock({ trendMode }) {
  const [data, setData] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(false);

  useEffect(() => {
    let isMounted = true;
    setLoading(true);
    setError(false);

    analyticsApi
      .trendSummary(trendMode)
      .then((res) => {
        if (isMounted) {
          setData(res?.data ?? []);
        }
      })
      .catch((err) => {
        console.error("Failed to load trend summary:", err);
        if (isMounted) {
          setError(true);
          setData([]);
        }
      })
      .finally(() => {
        if (isMounted) setLoading(false);
      });

    return () => {
      isMounted = false;
    };
  }, [trendMode]);

  // Aggregate totals for header banner
  const totalIncome = data.reduce((acc, curr) => acc + (curr.income ?? 0), 0);
  const totalExpense = data.reduce((acc, curr) => acc + (curr.expense ?? 0), 0);
  const netSurplus = totalIncome - totalExpense;

  // Loading Skeleton
  if (loading) {
    return (
      <div className="box p-5">
        <div className="is-flex is-justify-content-space-between mb-4">
          <div style={{ width: "40%", height: "24px", background: "#eee", borderRadius: "4px" }} />
          <div style={{ width: "20%", height: "24px", background: "#eee", borderRadius: "4px" }} />
        </div>
        <div style={{ width: "100%", height: "240px", background: "#f5f5f5", borderRadius: "6px" }} />
      </div>
    );
  }

  // Error state or Empty State
  if (error || !data.length) {
    return (
      <div className="box p-5 has-text-centered">
        <span className="icon is-large has-text-grey-light mb-2">
          <i className="fas fa-chart-line fa-2x" />
        </span>
        <p className="has-text-grey">No trend data available for this timeline.</p>
      </div>
    );
  }

  return (
    <div className="box p-4">
      {/* Header & Quick Stats */}
      <div className="level is-mobile mb-4">
        <div className="level-left">
          <div>
            <h3 className="title is-5 mb-1">
              <span className="mr-2">📈</span> Income vs. Expense Trend
            </h3>
            <p className="subtitle is-7 has-text-grey">
              Granularity: <span className="is-capitalized has-text-weight-bold">{trendMode}</span>
            </p>
          </div>
        </div>

        {/* Quick Summary Pill */}
        <div className="level-right">
          <div className="tags has-addons mb-0">
            <span className="tag is-light">Net</span>
            <span
              className={`tag is-bold ${
                netSurplus >= 0 ? "is-success" : "is-danger"
              }`}
            >
              ₹{netSurplus.toLocaleString("en-IN", { maximumFractionDigits: 0 })}
            </span>
          </div>
        </div>
      </div>

      {/* Chart Canvas */}
      <div style={{ width: "100%", height: 320 }}>
        <ResponsiveContainer width="100%" height="100%">
          <AreaChart data={data} margin={{ top: 10, right: 10, left: -10, bottom: 0 }}>
            <defs>
              <linearGradient id="colorIncome" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#48c774" stopOpacity={0.3} />
                <stop offset="95%" stopColor="#48c774" stopOpacity={0} />
              </linearGradient>
              <linearGradient id="colorExpense" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#ff3860" stopOpacity={0.3} />
                <stop offset="95%" stopColor="#ff3860" stopOpacity={0} />
              </linearGradient>
            </defs>

            <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#ededed" />

            <XAxis
              dataKey="period"
              tick={{ fontSize: 12, fill: "#7a7a7a" }}
              tickLine={false}
              axisLine={{ stroke: "#dbdbdb" }}
            />

            <YAxis
              tick={{ fontSize: 12, fill: "#7a7a7a" }}
              tickFormatter={formatCurrency}
              tickLine={false}
              axisLine={false}
            />

            <Tooltip content={<CustomTooltip />} />
            <Legend verticalAlign="top" height={36} align="right" iconType="circle" />

            <Area
              type="monotone"
              dataKey="income"
              name="Income"
              stroke="#48c774"
              strokeWidth={2.5}
              fillOpacity={1}
              fill="url(#colorIncome)"
              activeDot={{ r: 6 }}
            />

            <Area
              type="monotone"
              dataKey="expense"
              name="Expense"
              stroke="#ff3860"
              strokeWidth={2.5}
              fillOpacity={1}
              fill="url(#colorExpense)"
              activeDot={{ r: 6 }}
            />
          </AreaChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}

TrendChartBlock.propTypes = {
  trendMode: PropTypes.string.isRequired,
};
