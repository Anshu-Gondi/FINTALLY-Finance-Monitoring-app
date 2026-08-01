import { useState } from "react";
import PropTypes from "prop-types";
import {
  PieChart,
  Pie,
  Cell,
  Tooltip,
  ResponsiveContainer,
  Sector,
} from "recharts";

// Color palettes for income vs expense categories
const EXPENSE_COLORS = [
  "#3273dc", // Link Blue
  "#ff3860", // Danger Red
  "#ff8c00", // Dark Orange
  "#b86bff", // Purple
  "#ffdd57", // Yellow/Gold
  "#9b59b6", // Amethyst
];

const INCOME_COLORS = [
  "#48c774", // Success Green
  "#00d1b2", // Teal
  "#23d160", // Bright Green
  "#00c4a7", // Dark Teal
];

// Custom Tooltip component
const CustomTooltip = ({ active, payload, totalExpense, totalIncome }) => {
  if (active && payload && payload.length) {
    const data = payload[0].payload;
    const value = data.total ?? 0;
    const isIncome = data.tx_type?.toLowerCase() === "income";

    const totalBase = isIncome ? totalIncome : totalExpense;
    const pct = totalBase > 0 ? ((value / totalBase) * 100).toFixed(1) : 0;

    return (
      <div className="box p-3 shadow-sm border" style={{ minWidth: "160px" }}>
        <div className="is-flex is-align-items-center mb-1">
          <span
            className={`tag is-small ${
              isIncome ? "is-success is-light" : "is-danger is-light"
            } mr-2`}
          >
            {isIncome ? "INCOME" : "EXPENSE"}
          </span>
          <strong className="is-size-7">{data.category || "Uncategorized"}</strong>
        </div>

        <p
          className={`is-size-6 has-text-weight-bold is-family-monospace ${
            isIncome ? "has-text-success-dark" : "has-text-danger-dark"
          } mb-0`}
        >
          {isIncome ? "+" : "-"}₹
          {value.toLocaleString("en-IN", {
            minimumFractionDigits: 2,
            maximumFractionDigits: 2,
          })}
        </p>

        <p className="is-size-7 has-text-grey mt-1">
          {pct}% of {isIncome ? "Income" : "Expense"} total
        </p>
      </div>
    );
  }
  return null;
};

CustomTooltip.propTypes = {
  active: PropTypes.bool,
  payload: PropTypes.array,
  totalExpense: PropTypes.number,
  totalIncome: PropTypes.number,
};

// Render enlarged sector when active/hovered
const renderActiveShape = (props) => {
  const { cx, cy, innerRadius, outerRadius, startAngle, endAngle, fill } = props;
  return (
    <g>
      <Sector
        cx={cx}
        cy={cy}
        innerRadius={innerRadius - 2}
        outerRadius={outerRadius + 6}
        startAngle={startAngle}
        endAngle={endAngle}
        fill={fill}
      />
    </g>
  );
};

export default function PieChartBlock({ data }) {
  const [activeIndex, setActiveIndex] = useState(null);
  const [filterType, setFilterType] = useState("all"); // 'all' | 'expense' | 'income'

  if (!data || !data.length) {
    return (
      <div className="box p-5 has-text-centered">
        <span className="icon is-large has-text-grey-light mb-2">
          <i className="fas fa-chart-pie fa-2x" />
        </span>
        <p className="has-text-grey">No transaction category data available.</p>
      </div>
    );
  }

  // Calculate global totals
  const totalExpense = data
    .filter((item) => item.tx_type?.toLowerCase() !== "income")
    .reduce((acc, item) => acc + (item.total ?? 0), 0);

  const totalIncome = data
    .filter((item) => item.tx_type?.toLowerCase() === "income")
    .reduce((acc, item) => acc + (item.total ?? 0), 0);

  // Filter data based on tab selection
  const filteredData = data
    .filter((item) => {
      const isIncome = item.tx_type?.toLowerCase() === "income";
      if (filterType === "expense") return !isIncome;
      if (filterType === "income") return isIncome;
      return true; // 'all'
    })
    .sort((a, b) => (b.total ?? 0) - (a.total ?? 0));

  // Determine cell color based on tx_type
  const getCellColor = (item, idx) => {
    const isIncome = item.tx_type?.toLowerCase() === "income";
    if (isIncome) {
      return INCOME_COLORS[idx % INCOME_COLORS.length];
    }
    return EXPENSE_COLORS[idx % EXPENSE_COLORS.length];
  };

  return (
    <div className="box p-4">
      {/* Card Header & Filter Switcher */}
      <div className="level is-mobile mb-4">
        <div className="level-left">
          <div>
            <h3 className="title is-5 mb-1">
              <span className="mr-2">🍕</span> Category Breakdown
            </h3>
            <p className="subtitle is-7 has-text-grey">
              Combined Income & Expense distribution
            </p>
          </div>
        </div>

        {/* Filter Buttons */}
        <div className="level-right">
          <div className="buttons has-addons are-small mb-0">
            <button
              className={`button ${filterType === "all" ? "is-link is-selected" : ""}`}
              onClick={() => setFilterType("all")}
            >
              All
            </button>
            <button
              className={`button ${filterType === "expense" ? "is-danger is-selected" : ""}`}
              onClick={() => setFilterType("expense")}
            >
              Expenses
            </button>
            <button
              className={`button ${filterType === "income" ? "is-success is-selected" : ""}`}
              onClick={() => setFilterType("income")}
            >
              Income
            </button>
          </div>
        </div>
      </div>

      <div className="columns is-vcentered">
        {/* Donut Chart Canvas */}
        <div className="column is-12-mobile is-6-tablet">
          <div style={{ width: "100%", height: 260, position: "relative" }}>
            <ResponsiveContainer width="100%" height="100%">
              <PieChart>
                <Pie
                  data={filteredData}
                  dataKey="total"
                  nameKey="category"
                  cx="50%"
                  cy="50%"
                  innerRadius={65}
                  outerRadius={95}
                  paddingAngle={3}
                  activeIndex={activeIndex}
                  activeShape={renderActiveShape}
                  onMouseEnter={(_, index) => setActiveIndex(index)}
                  onMouseLeave={() => setActiveIndex(null)}
                >
                  {filteredData.map((entry, index) => (
                    <Cell
                      key={`cell-${index}`}
                      fill={getCellColor(entry, index)}
                      style={{ outline: "none" }}
                    />
                  ))}
                </Pie>
                <Tooltip
                  content={
                    <CustomTooltip
                      totalExpense={totalExpense}
                      totalIncome={totalIncome}
                    />
                  }
                />
              </PieChart>
            </ResponsiveContainer>

            {/* Donut Center Overlay */}
            <div
              style={{
                position: "absolute",
                top: "50%",
                left: "50%",
                transform: "translate(-50%, -50%)",
                textAlign: "center",
                pointerEvents: "none",
              }}
            >
              <p className="is-size-7 has-text-grey mb-0">
                {filterType === "income"
                  ? "Total Income"
                  : filterType === "expense"
                  ? "Total Expense"
                  : "Net Surplus"}
              </p>
              <p
                className={`has-text-weight-bold is-size-6 is-family-monospace mb-0 ${
                  filterType === "all"
                    ? totalIncome - totalExpense >= 0
                      ? "has-text-success"
                      : "has-text-danger"
                    : filterType === "income"
                    ? "has-text-success"
                    : "has-text-danger"
                }`}
              >
                ₹
                {Math.abs(
                  filterType === "income"
                    ? totalIncome
                    : filterType === "expense"
                    ? totalExpense
                    : totalIncome - totalExpense
                ).toLocaleString("en-IN", { maximumFractionDigits: 0 })}
              </p>
            </div>
          </div>
        </div>

        {/* Custom Category Side Legend */}
        <div className="column is-12-mobile is-6-tablet">
          <div
            style={{ maxHeight: "240px", overflowY: "auto", paddingRight: "4px" }}
          >
            {filteredData.map((item, idx) => {
              const amount = item.total ?? 0;
              const isIncome = item.tx_type?.toLowerCase() === "income";
              const totalBase = isIncome ? totalIncome : totalExpense;
              const pct = totalBase > 0 ? ((amount / totalBase) * 100).toFixed(1) : "0.0";
              const color = getCellColor(item, idx);
              const isHovered = activeIndex === idx;

              return (
                <div
                  key={idx}
                  className={`is-flex is-align-items-center is-justify-content-space-between p-2 mb-1 is-radius-small ${
                    isHovered ? "has-background-white-ter" : ""
                  }`}
                  style={{
                    cursor: "pointer",
                    transition: "background 0.2s ease",
                  }}
                  onMouseEnter={() => setActiveIndex(idx)}
                  onMouseLeave={() => setActiveIndex(null)}
                >
                  <div className="is-flex is-align-items-center">
                    <span
                      style={{
                        width: "10px",
                        height: "10px",
                        borderRadius: "50%",
                        backgroundColor: color,
                        display: "inline-block",
                        marginRight: "10px",
                        flexShrink: 0,
                      }}
                    />
                    <div>
                      <div className="is-flex is-align-items-center">
                        <p className="is-size-7 has-text-weight-semibold has-text-dark mb-0 mr-1">
                          {item.category || "Uncategorized"}
                        </p>
                        <span
                          className={`tag is-small style-tag ${
                            isIncome ? "is-success is-light" : "is-danger is-light"
                          }`}
                          style={{ fontSize: "0.65rem", height: "1.4em" }}
                        >
                          {isIncome ? "INC" : "EXP"}
                        </span>
                      </div>
                      <p className="is-size-7 has-text-grey-light mb-0">{pct}%</p>
                    </div>
                  </div>

                  <span
                    className={`is-size-7 is-family-monospace has-text-weight-bold ${
                      isIncome ? "has-text-success-dark" : "has-text-dark"
                    }`}
                  >
                    {isIncome ? "+" : "-"}₹
                    {amount.toLocaleString("en-IN", { maximumFractionDigits: 0 })}
                  </span>
                </div>
              );
            })}
          </div>
        </div>
      </div>
    </div>
  );
}

PieChartBlock.propTypes = {
  data: PropTypes.arrayOf(
    PropTypes.shape({
      category: PropTypes.string,
      total: PropTypes.number,
      tx_type: PropTypes.string, // e.g., 'income' or 'expense'
    })
  ),
};

PieChartBlock.defaultProps = {
  data: [],
};
