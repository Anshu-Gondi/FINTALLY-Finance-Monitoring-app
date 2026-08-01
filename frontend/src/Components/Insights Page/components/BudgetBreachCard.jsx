import PropTypes from "prop-types";

export default function BudgetBreachCard({ data, loading }) {
  // Skeleton Loading State
  if (loading) {
    return (
      <div className="box p-5">
        <div className="is-flex is-align-items-center mb-3">
          <span className="icon is-small has-text-info mr-2">
            <i className="fas fa-spinner fa-pulse" />
          </span>
          <span className="has-text-weight-semibold">Running Monte Carlo Simulations...</span>
        </div>
        <div className="skeleton-lines">
          <progress className="progress is-small is-primary" max="100">15%</progress>
        </div>
      </div>
    );
  }

  // Empty State
  if (!data) {
    return (
      <div className="box p-5 has-text-centered">
        <span className="icon is-large has-text-grey-light mb-2">
          <i className="fas fa-wallet fa-2x" />
        </span>
        <p className="has-text-grey">No budget breach projection data available.</p>
      </div>
    );
  }

  const probPercent = (data.breach_probability ?? 0) * 100;

  // Dynamic Risk Level Assessment
  let riskClass = "is-success";
  let riskText = "Low Risk";
  let riskMessage = "Your spending is currently on track to stay within budget.";

  if (probPercent >= 70) {
    riskClass = "is-danger";
    riskText = "High Risk";
    riskMessage = "Urgent: High chance of exceeding your budget target soon!";
  } else if (probPercent >= 30) {
    riskClass = "is-warning";
    riskText = "Moderate Risk";
    riskMessage = "Keep an eye on non-essential spending for the rest of the period.";
  }

  return (
    <div className="box p-5">
      {/* Header */}
      <div className="level is-mobile mb-3">
        <div className="level-left">
          <h3 className="title is-5 mb-0">
            <span className="mr-2">🎯</span> Budget Breach Forecast
          </h3>
        </div>
        <div className="level-right">
          <span className={`tag ${riskClass} is-medium is-rounded has-text-weight-bold`}>
            {riskText}
          </span>
        </div>
      </div>

      <p className="is-size-7 has-text-grey mb-4">{riskMessage}</p>

      {/* Probability Gauge Bar */}
      <div className="mb-4">
        <div className="is-flex is-justify-content-space-between mb-1">
          <span className="is-size-7 has-text-weight-semibold">Probability of Breach</span>
          <span className="is-size-7 has-text-weight-bold">{probPercent.toFixed(1)}%</span>
        </div>
        <progress
          className={`progress ${riskClass} is-medium`}
          value={probPercent}
          max="100"
        >
          {probPercent.toFixed(1)}%
        </progress>
      </div>

      {/* Primary Metrics Grid */}
      <div className="columns is-mobile is-multiline border-top pt-3">
        {/* Expected Spend */}
        <div className="column is-12-mobile is-4-tablet">
          <div className="has-background-light p-3 is-radius-small">
            <p className="heading has-text-grey mb-1">Expected Spend</p>
            <p className="title is-5 is-family-monospace has-text-dark">
              ₹{(data.expected_spend ?? 0).toLocaleString("en-IN", {
                maximumFractionDigits: 0,
              })}
            </p>
          </div>
        </div>

        {/* Expected Days to Breach (P50) */}
        <div className="column is-6-mobile is-4-tablet">
          <div className="has-background-light p-3 is-radius-small">
            <p className="heading has-text-grey mb-1" title="50% likelihood target will be reached in this timeframe">
              Est. Days to Breach ℹ️
            </p>
            <p className="title is-5 has-text-dark">
              {typeof data.p50_days_to_breach === "number"
                ? `${data.p50_days_to_breach} Days`
                : "Safe"}
            </p>
          </div>
        </div>

        {/* Worst Case Days to Breach (P95) */}
        <div className="column is-6-mobile is-4-tablet">
          <div className="has-background-light p-3 is-radius-small">
            <p className="heading has-text-grey mb-1" title="95% worst-case speed scenario">
              Worst-case Breach ℹ️
            </p>
            <p className="title is-5 has-text-dark">
              {typeof data.p95_days_to_breach === "number"
                ? `${data.p95_days_to_breach} Days`
                : "Safe"}
            </p>
          </div>
        </div>
      </div>
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
