import { useState } from "react";
import PropTypes from "prop-types";
import { analyticsApi } from "../../../services/api";

export default function GoalCard() {
  const [targetInput, setTargetInput] = useState("");
  const [data, setData] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);

  const handleSubmit = async (overrideAmount) => {
    const amount = parseFloat(overrideAmount ?? targetInput);
    if (!amount || amount <= 0) return;

    setLoading(true);
    setError(null);
    try {
      const res = await analyticsApi.goalProjection(amount);
      setData(res);
    } catch (err) {
      setError(err?.message || "Failed to calculate goal projection.");
    } finally {
      setLoading(false);
    }
  };

  // Helper to handle quick preset button clicks
  const handleQuickPreset = (val) => {
    setTargetInput(val.toString());
    handleSubmit(val);
  };

  // Helper to format months into years and months
  const formatTimeframe = (months) => {
    if (months === null || months === undefined || months < 0) {
      return "Not achievable at current rate";
    }
    if (months === 0) return "Goal already reached! 🎉";

    const yrs = Math.floor(months / 12);
    const remainingMonths = Math.round(months % 12);

    if (yrs === 0) return `${remainingMonths} ${remainingMonths === 1 ? "month" : "months"}`;
    if (remainingMonths === 0) return `${yrs} ${yrs === 1 ? "year" : "years"}`;
    return `${yrs} yrs, ${remainingMonths} mos`;
  };

  // Calculate percentage of target already saved
  const currentSavings = data?.current_savings ?? 0;
  const targetAmount = data?.target_amount ?? 0;
  const progressPct = targetAmount > 0 ? Math.min(100, (currentSavings / targetAmount) * 100) : 0;
  const isAchievable = (data?.months_to_goal ?? -1) >= 0;

  return (
    <div className="box p-5">
      {/* Header */}
      <div className="level is-mobile mb-3">
        <div className="level-left">
          <h3 className="title is-5 mb-0">
            <span className="mr-2">🎯</span> Goal Reachability Calculator
          </h3>
        </div>
      </div>
      <p className="is-size-7 has-text-grey mb-4">
        Project how long it will take to reach your financial goal based on current savings rate.
      </p>

      {/* Input Field & Submit */}
      <div className="field has-addons mb-2">
        <div className="control has-icons-left is-expanded">
          <input
            className="input"
            type="number"
            placeholder="Enter target amount (e.g. 500000)"
            value={targetInput}
            onChange={(e) => setTargetInput(e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && handleSubmit()}
            min="1"
          />
          <span className="icon is-small is-left">₹</span>
        </div>
        <div className="control">
          <button
            className={`button is-link ${loading ? "is-loading" : ""}`}
            onClick={() => handleSubmit()}
            disabled={loading || !targetInput}
          >
            Calculate
          </button>
        </div>
      </div>

      {/* Quick Amount Chips */}
      <div className="buttons are-small mb-4">
        <span className="is-size-7 has-text-grey mr-2">Quick set:</span>
        <button className="button is-light is-rounded" onClick={() => handleQuickPreset(50000)}>
          ₹50k
        </button>
        <button className="button is-light is-rounded" onClick={() => handleQuickPreset(100000)}>
          ₹1 Lakh
        </button>
        <button className="button is-light is-rounded" onClick={() => handleQuickPreset(500000)}>
          ₹5 Lakhs
        </button>
        <button className="button is-light is-rounded" onClick={() => handleQuickPreset(1000000)}>
          ₹10 Lakhs
        </button>
      </div>

      {/* Error State */}
      {error && (
        <div className="notification is-danger is-light py-2 px-3 is-size-7 mb-3">
          ⚠️ {error}
        </div>
      )}

      {/* Projection Results */}
      {data && (
        <div className="mt-4 pt-3 border-top">
          {/* Status Message */}
          <div
            className={`notification ${
              isAchievable ? "is-success is-light" : "is-warning is-light"
            } py-2 px-3 is-size-7 mb-4`}
          >
            {isAchievable ? (
              <span>
                <strong>Timeline Estimate:</strong> At your current saving rate of ₹
                {(data.monthly_savings ?? 0).toLocaleString("en-IN")}/mo, you will achieve this goal in{" "}
                <strong>{formatTimeframe(data.months_to_goal)}</strong>.
              </span>
            ) : (
              <span>
                ⚠️ <strong>Current Savings Deficit:</strong> Your monthly net savings are currently zero or negative. Increase monthly cashflow to compute timeline.
              </span>
            )}
          </div>

          {/* Goal Progress Bar */}
          <div className="mb-4">
            <div className="is-flex is-justify-content-space-between mb-1">
              <span className="is-size-7 has-text-weight-semibold">Goal Coverage</span>
              <span className="is-size-7 has-text-weight-bold">{progressPct.toFixed(1)}%</span>
            </div>
            <progress
              className="progress is-link is-medium"
              value={progressPct}
              max="100"
            >
              {progressPct.toFixed(1)}%
            </progress>
          </div>

          {/* Metric Tiles */}
          <div className="columns is-mobile is-multiline">
            <div className="column is-6-mobile is-3-tablet">
              <div className="has-background-light p-3 is-radius-small">
                <p className="heading has-text-grey mb-1">Target Goal</p>
                <p className="title is-6 is-family-monospace has-text-dark">
                  ₹{(data.target_amount ?? 0).toLocaleString("en-IN")}
                </p>
              </div>
            </div>

            <div className="column is-6-mobile is-3-tablet">
              <div className="has-background-light p-3 is-radius-small">
                <p className="heading has-text-grey mb-1">Current Savings</p>
                <p className="title is-6 is-family-monospace has-text-dark">
                  ₹{(data.current_savings ?? 0).toLocaleString("en-IN")}
                </p>
              </div>
            </div>

            <div className="column is-6-mobile is-3-tablet">
              <div className="has-background-light p-3 is-radius-small">
                <p className="heading has-text-grey mb-1">Monthly Surplus</p>
                <p
                  className={`title is-6 is-family-monospace ${
                    (data.monthly_savings ?? 0) <= 0
                      ? "has-text-danger"
                      : "has-text-success"
                  }`}
                >
                  ₹{(data.monthly_savings ?? 0).toLocaleString("en-IN")}
                </p>
              </div>
            </div>

            <div className="column is-6-mobile is-3-tablet">
              <div className="has-background-light p-3 is-radius-small">
                <p className="heading has-text-grey mb-1">Estimated Time</p>
                <p className="title is-6 has-text-dark">
                  {isAchievable ? formatTimeframe(data.months_to_goal) : "N/A"}
                </p>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

GoalCard.propTypes = {
  // Component manages state internally
};
