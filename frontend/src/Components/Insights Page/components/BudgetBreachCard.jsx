import PropTypes from "prop-types";

// BudgetBreachCard.jsx
export default function BudgetBreachCard({ data, loading }) {
  if (loading) return <p>Running simulations…</p>;
  if (!data) return <p>No budget data available.</p>;

  return (
    <div className="box">
      <p>
        <strong>Probability of Breach:</strong>{" "}
        {((data.breach_probability ?? 0) * 100).toFixed(1)}%
      </p>
      <p>
        <strong>Expected Spend:</strong>{" "}
        ₹{(data.expected_spend ?? 0).toFixed(0)}
      </p>
      <p>
        <strong>Days to Breach (P50):</strong>{" "}
        {data.p50_days_to_breach ?? "Safe"}
      </p>
    </div>
  );
}

BudgetBreachCard.propTypes = {
  data: PropTypes.shape({
    breach_probability: PropTypes.number,
    expected_spend: PropTypes.number,
    p95_days_to_breach: PropTypes.number,
    p50_days_to_breach: PropTypes.number,

  }),
  loading: PropTypes.bool,
};

BudgetBreachCard.defaultProps = {
  data: null,
  loading: false,
};