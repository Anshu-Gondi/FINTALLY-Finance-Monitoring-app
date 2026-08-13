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

// General color palette for categories
const CATEGORY_COLORS = [
  "#3273dc", // Link Blue
  "#ff3860", // Danger Red
  "#ff8c00", // Dark Orange
  "#b86bff", // Purple
  "#ffdd57", // Yellow/Gold
  "#9b59b6", // Amethyst
  "#48c774", // Success Green
  "#00d1b2", // Teal
  "#3298dc", // Cyan
  "#00c4a7", // Dark Teal
];

// Helper to safely extract total numerical value
const getDbValue = (item) => {
  const val = item?.price ?? item?.total ?? item?.amount ?? 0;
  const num = typeof val === "string" ? parseFloat(val) : val;
  return isNaN(num) ? 0 : Math.abs(num);
};

// Custom Tooltip Component
const CustomTooltip = ({ active, payload, overallTotal }) => {
  if (active && payload && payload.length) {
    const data = payload[0].payload;
    const value = data.totalValue;
    const pct = overallTotal > 0 ? ((value / overallTotal) * 100).toFixed(1) : 0;

    return (
      <div className="box p-3 shadow-sm border" style={{ minWidth: "160px" }}>
        <div className="is-flex is-align-items-center mb-1">
          <strong className="is-size-7">{data.category || "Uncategorized"}</strong>
        </div>

        <p className="is-size-6 has-text-weight-bold is-family-monospace has-text-dark mb-0">
          ₹
          {value.toLocaleString("en-IN", {
            minimumFractionDigits: 2,
            maximumFractionDigits: 2,
          })}
        </p>

        <p className="is-size-7 has-text-grey mt-1 mb-0">
          {pct}% of Total Volume
        </p>
        {data.count !== undefined && (
          <p className="is-size-7 has-text-grey">
            {data.count} {data.count === 1 ? "transaction" : "transactions"}
          </p>
        )}
      </div>
    );
  }
  return null;
};

CustomTooltip.propTypes = {
  active: PropTypes.bool,
  payload: PropTypes.array,
  overallTotal: PropTypes.number,
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

  // Aggregate total transaction value and count by category
  const aggregatedMap = data.reduce((acc, item) => {
    const categoryName = item.category || item.name || "Uncategorized";
    const value = getDbValue(item);
    const count = item.count ?? 1;

    if (!acc[categoryName]) {
      acc[categoryName] = {
        category: categoryName,
        totalValue: 0,
        count: 0,
      };
    }

    acc[categoryName].totalValue += value;
    acc[categoryName].count += count;

    return acc;
  }, {});

  const categoryData = Object.values(aggregatedMap).sort(
    (a, b) => b.totalValue - a.totalValue
  );

  const overallTotalValue = categoryData.reduce(
    (acc, item) => acc + item.totalValue,
    0
  );

  const overallTotalCount = categoryData.reduce(
    (acc, item) => acc + item.count,
    0
  );

  return (
    <div className="box p-4">
      {/* Card Header */}
      <div className="level is-mobile mb-4">
        <div className="level-left">
          <div>
            <h3 className="title is-5 mb-1">
              <span className="mr-2">🍕</span> Category Volume
            </h3>
            <p className="subtitle is-7 has-text-grey">
              Total transaction volume aggregated by category
            </p>
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
                  data={categoryData}
                  dataKey="totalValue"
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
                  {categoryData.map((entry, index) => (
                    <Cell
                      key={`cell-${index}`}
                      fill={CATEGORY_COLORS[index % CATEGORY_COLORS.length]}
                      style={{ outline: "none" }}
                    />
                  ))}
                </Pie>
                <Tooltip
                  content={
                    <CustomTooltip overallTotal={overallTotalValue} />
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
              <p className="is-size-7 has-text-grey mb-0">Total Volume</p>
              <p className="has-text-weight-bold is-size-6 is-family-monospace mb-0 has-text-link">
                ₹
                {overallTotalValue.toLocaleString("en-IN", {
                  maximumFractionDigits: 0,
                })}
              </p>
              <p className="is-size-7 has-text-grey-light mb-0">
                {overallTotalCount} txns
              </p>
            </div>
          </div>
        </div>

        {/* Custom Category Side Legend */}
        <div className="column is-12-mobile is-6-tablet">
          <div
            style={{ maxHeight: "240px", overflowY: "auto", paddingRight: "4px" }}
          >
            {categoryData.map((item, idx) => {
              const pct =
                overallTotalValue > 0
                  ? ((item.totalValue / overallTotalValue) * 100).toFixed(1)
                  : "0.0";
              const color = CATEGORY_COLORS[idx % CATEGORY_COLORS.length];
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
                      <p className="is-size-7 has-text-weight-semibold has-text-dark mb-0">
                        {item.category}
                      </p>
                      <p className="is-size-7 has-text-grey-light mb-0">
                        {pct}% • {item.count} {item.count === 1 ? "txn" : "txns"}
                      </p>
                    </div>
                  </div>

                  <span className="is-size-7 is-family-monospace has-text-weight-bold has-text-dark">
                    ₹{item.totalValue.toLocaleString("en-IN", { maximumFractionDigits: 0 })}
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
      name: PropTypes.string,
      total: PropTypes.oneOfType([PropTypes.number, PropTypes.string]),
      price: PropTypes.oneOfType([PropTypes.number, PropTypes.string]),
      amount: PropTypes.oneOfType([PropTypes.number, PropTypes.string]),
      count: PropTypes.number,
    })
  ),
};

PieChartBlock.defaultProps = {
  data: [],
};
