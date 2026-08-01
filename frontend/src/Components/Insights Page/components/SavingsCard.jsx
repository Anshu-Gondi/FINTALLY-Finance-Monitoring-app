import { useState, useEffect } from "react";
import PropTypes from "prop-types";
import { analyticsApi } from "../../../services/api";

export default function SavingsCard() {
  const [data, setData] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(false);

  const fetchSavingsData = async () => {
    setLoading(true);
    setError(false);
    try {
      const res = await analyticsApi.savingsOptimization();
      setData(res);
    } catch (err) {
      console.error("Failed to fetch savings optimization data:", err);
      setError(true);
      setData(null);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchSavingsData();
  }, []);

  // Skeleton Loader State
  if (loading) {
    return (
      <div className="box p-5">
        <div className="is-flex is-align-items-center mb-4">
          <div className="mr-3" style={{ width: "32px", height: "32px", borderRadius: "50%", background: "#eee" }} />
          <div style={{ width: "60%", height: "20px", background: "#eee", borderRadius: "4px" }} />
        </div>
        <div style={{ width: "100%", height: "12px", background: "#eee", borderRadius: "4px" }} className="mb-3" />
        <div style={{ width: "80%", height: "12px", background: "#eee", borderRadius: "4px" }} />
      </div>
    );
  }

  // Error State
  if (error || !data) {
    return (
      <div className="box p-5 has-text-centered">
        <span className="icon is-medium has-text-warning mb-2">
          <i className="fas fa-exclamation-triangle fa-lg" />
        </span>
        <p className="has-text-grey is-size-7 mb-3">Unable to load savings insights.</p>
        <button className="button is-small is-light" onClick={fetchSavingsData}>
          Retry
        </button>
      </div>
    );
  }

  // Extract variables with fallbacks
  const savingRate = data.saving_rate_percent ?? 0;
  const healthScore = data.financial_health_score ?? 0;
  const monthlySavings = data.monthly_savings ?? null;
  const recommendations = data.suggestions || data.recommendations || [];

  // Financial Health Score Color & Badge Logic
  const getHealthMeta = (score) => {
    if (score >= 80) return { label: "Excellent", color: "is-success", textColor: "has-text-success" };
    if (score >= 60) return { label: "Good", color: "is-info", textColor: "has-text-info" };
    if (score >= 40) return { label: "Needs Work", color: "is-warning", textColor: "has-text-warning-dark" };
    return { label: "Critical", color: "is-danger", textColor: "has-text-danger" };
  };

  // Savings Rate Assessment
  const getSavingsRateMeta = (rate) => {
    if (rate >= 30) return { status: "Outstanding", color: "is-success" };
    if (rate >= 20) return { status: "Healthy Target met", color: "is-link" };
    if (rate >= 10) return { status: "Moderate", color: "is-warning" };
    return { status: "Low Savings", color: "is-danger" };
  };

  const healthMeta = getHealthMeta(healthScore);
  const savingMeta = getSavingsRateMeta(savingRate);

  return (
    <div className="box p-5">
      {/* Header */}
      <div className="level is-mobile mb-4">
        <div className="level-left">
          <div>
            <h3 className="title is-5 mb-1">
              <span className="mr-2">💡</span> Savings & Health Insights
            </h3>
            <p className="subtitle is-7 has-text-grey">
              AI-driven optimization & financial health assessment
            </p>
          </div>
        </div>
        <div className="level-right">
          <span className={`tag ${healthMeta.color} is-light has-text-weight-bold`}>
            {healthMeta.label}
          </span>
        </div>
      </div>

      {/* Primary Metrics: Health Score & Saving Rate */}
      <div className="columns is-mobile mb-3">
        {/* Metric 1: Financial Health Score */}
        <div className="column is-6">
          <div className="has-background-white-ter p-3 is-radius-small h-100">
            <span className="heading has-text-grey mb-1">Health Score</span>
            <div className="is-flex is-align-items-baseline">
              <span className={`title is-4 is-family-monospace mr-1 ${healthMeta.textColor}`}>
                {healthScore.toFixed(0)}
              </span>
              <span className="is-size-7 has-text-grey">/ 100</span>
            </div>
            {/* Health Score Gauge Bar */}
            <progress
              className={`progress ${healthMeta.color} is-small mt-2 mb-0`}
              value={Math.min(100, Math.max(0, healthScore))}
              max="100"
            />
          </div>
        </div>

        {/* Metric 2: Saving Rate Percent */}
        <div className="column is-6">
          <div className="has-background-white-ter p-3 is-radius-small h-100">
            <span className="heading has-text-grey mb-1">Saving Rate</span>
            <div className="is-flex is-align-items-baseline">
              <span className="title is-4 is-family-monospace has-text-dark mr-1">
                {savingRate.toFixed(1)}%
              </span>
            </div>
            {/* Saving Rate Benchmark Bar */}
            <progress
              className={`progress ${savingMeta.color} is-small mt-2 mb-0`}
              value={Math.min(100, Math.max(0, savingRate))}
              max="50" // Benchmark target scale up to 50%
            />
          </div>
        </div>
      </div>

      {/* Monthly Net Savings Amount (If returned by API) */}
      {monthlySavings !== null && (
        <div className="is-flex is-justify-content-space-between is-align-items-center p-3 mb-3 has-background-light is-radius-small">
          <span className="is-size-7 has-text-grey-dark">Estimated Monthly Surplus:</span>
          <span className="is-size-6 is-family-monospace has-text-weight-bold has-text-success-dark">
            ₹{monthlySavings.toLocaleString("en-IN", { maximumFractionDigits: 0 })}
          </span>
        </div>
      )}

      {/* Smart Recommendations List (If available) */}
      {recommendations.length > 0 && (
        <div className="mt-4 pt-3 border-top">
          <p className="heading has-text-grey mb-2">Optimization Tips</p>
          <div className="content is-small">
            {recommendations.slice(0, 3).map((tip, idx) => (
              <div key={idx} className="is-flex is-align-items-start mb-2">
                <span className="mr-2">⚡</span>
                <span className="has-text-grey-dark">{tip}</span>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}

SavingsCard.propTypes = {
  // Component manages fetching internally
};
