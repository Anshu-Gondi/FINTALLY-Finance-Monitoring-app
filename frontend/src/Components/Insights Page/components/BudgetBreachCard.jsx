import PropTypes from "prop-types";

export default function BudgetBreachCard({ data, loading }) {
  if (loading) return <p>Running simulations…</p>;
  if (!data) return <p>No data</p>;

  return (
    <div>
      <p><strong>Probability of Breach:</strong> {(data.breachProbability * 100).toFixed(1)}%</p>
      <p><strong>Expected Spend:</strong> ₹{data.expectedSpend.toFixed(0)}</p>
      <p><strong>P95 Days to Breach:</strong> {data.p95DaysToBreach ?? "Safe"}</p>
    </div>
  );
}

BudgetBreachCard.propTypes = {
  data: PropTypes.shape({
    breachProbability: PropTypes.number,
    expectedSpend: PropTypes.number,
    p95DaysToBreach: PropTypes.number,
  }),
  loading: PropTypes.bool,
};

BudgetBreachCard.defaultProps = {
  data: null,
  loading: false,
};