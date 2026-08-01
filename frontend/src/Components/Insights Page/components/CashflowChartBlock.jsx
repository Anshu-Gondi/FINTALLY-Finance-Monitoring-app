import PropTypes from "prop-types";
import {
  AreaChart,
  Area,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
} from "recharts";

// Custom Tooltip for detailed information on hover
const CustomTooltip = ({ active, payload }) => {
  if (active && payload && payload.length) {
    const dataPoint = payload[0].payload;
    const balance = dataPoint.expected_balance ?? 0;
    const isNegative = balance < 0;

    return (
      <div className="box p-3 shadow-sm border" style={{ minWidth: "160px" }}>
        <p className="is-size-7 has-text-grey mb-1">
          <strong>Timeline:</strong> +{dataPoint.horizon_days} Days
        </p>
        <p
          className={`is-size-6 has-text-weight-bold is-family-monospace ${
            isNegative ? "has-text-danger" : "has-text-success"
          }`}
        >
          ₹{balance.toLocaleString("en-IN", {
            minimumFractionDigits: 2,
            maximumFractionDigits: 2,
          })}
        </p>
        <p className="is-size-7 has-text-grey-light mt-1">Expected Balance</p>
      </div>
    );
  }
  return null;
};

CustomTooltip.propTypes = {
  active: PropTypes.bool,
  payload: PropTypes.array,
};

export default function CashflowChartBlock({ data }) {
  const points = data?.points ?? [];

  // Empty State Component
  if (!points.length) {
    return (
      <div className="box p-5 has-text-centered">
        <span className="icon is-large has-text-grey-light mb-2">
          <i className="fas fa-chart-line fa-2x" />
        </span>
        <p className="has-text-grey">Cashflow forecast projection unavailable.</p>
      </div>
    );
  }

  // Format Large Numbers for Y-Axis Labels (e.g. ₹50k, ₹1.5L)
  const formatYAxis = (value) => {
    if (Math.abs(value) >= 100000) {
      return `₹${(value / 100000).toFixed(1)}L`;
    }
    if (Math.abs(value) >= 1000) {
      return `₹${(value / 1000).toFixed(0)}k`;
    }
    return `₹${value}`;
  };

  return (
    <div className="box p-4">
      {/* Header */}
      <div className="level is-mobile mb-4">
        <div className="level-left">
          <div>
            <h3 className="title is-5 mb-1">
              <span className="mr-2">📈</span> Cashflow Forecast
            </h3>
            <p className="subtitle is-7 has-text-grey">
              Predicted liquidity trajectory across future horizons
            </p>
          </div>
        </div>
      </div>

      {/* Horizon Projection Badges */}
      <div className="columns is-mobile is-multiline mb-3">
        {points.map((pt, idx) => (
          <div key={idx} className="column is-4-tablet is-12-mobile">
            <div className="has-background-light p-3 is-radius-small">
              <p className="heading has-text-grey mb-1">
                {pt.horizon_days} Days Projection
              </p>
              <p
                className={`title is-6 is-family-monospace ${
                  (pt.expected_balance ?? 0) < 0
                    ? "has-text-danger"
                    : "has-text-link-dark"
                }`}
              >
                ₹
                {(pt.expected_balance ?? 0).toLocaleString("en-IN", {
                  maximumFractionDigits: 0,
                })}
              </p>
            </div>
          </div>
        ))}
      </div>

      {/* Chart Canvas */}
      <div style={{ width: "100%", height: 320 }}>
        <ResponsiveContainer width="100%" height="100%">
          <AreaChart data={points} margin={{ top: 10, right: 20, left: 10, bottom: 0 }}>
            <defs>
              <linearGradient id="balanceGradient" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#3273dc" stopOpacity={0.3} />
                <stop offset="95%" stopColor="#3273dc" stopOpacity={0.0} />
              </linearGradient>
            </defs>
            <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#f0f0f0" />
            <XAxis
              dataKey="horizon_days"
              tickFormatter={(days) => `+${days} Days`}
              tick={{ fontSize: 12, fill: "#7a7a7a" }}
              axisLine={{ stroke: "#e0e0e0" }}
            />
            <YAxis
              tickFormatter={formatYAxis}
              tick={{ fontSize: 12, fill: "#7a7a7a" }}
              axisLine={{ stroke: "#e0e0e0" }}
            />
            <Tooltip content={<CustomTooltip />} />
            <Area
              type="monotone"
              dataKey="expected_balance"
              stroke="#3273dc"
              strokeWidth={2.5}
              fillOpacity={1}
              fill="url(#balanceGradient)"
              activeDot={{ r: 6, stroke: "#3273dc", strokeWidth: 2, fill: "#ffffff" }}
              name="Expected Balance"
            />
          </AreaChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}

CashflowChartBlock.propTypes = {
  data: PropTypes.shape({
    points: PropTypes.arrayOf(
      PropTypes.shape({
        horizon_days: PropTypes.number,
        expected_balance: PropTypes.number,
      })
    ),
  }),
};

CashflowChartBlock.defaultProps = {
  data: {
    points: [],
  },
};
