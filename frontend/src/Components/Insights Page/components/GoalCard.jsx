// GoalCard.jsx — takes target amount input, fetches from backend
import { useState } from "react";
import { analyticsApi } from "../../../services/api";

export default function GoalCard() {
  const [targetInput, setTargetInput] = useState("");
  const [data, setData]               = useState(null);
  const [loading, setLoading]         = useState(false);
  const [error, setError]             = useState(null);

  const handleSubmit = async () => {
    const amount = parseFloat(targetInput);
    if (!amount || amount <= 0) return;

    setLoading(true);
    setError(null);
    try {
      const res = await analyticsApi.goalProjection(amount);
      setData(res);
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="box">
      <h3 className="subtitle has-text-black">Goal Projection</h3>

      <div className="field has-addons">
        <div className="control is-expanded">
          <input
            className="input"
            type="number"
            placeholder="Enter target amount (₹)"
            value={targetInput}
            onChange={e => setTargetInput(e.target.value)}
            onKeyDown={e => e.key === "Enter" && handleSubmit()}
          />
        </div>
        <div className="control">
          <button
            className={`button is-link ${loading ? "is-loading" : ""}`}
            onClick={handleSubmit}
            disabled={loading}
          >
            Calculate
          </button>
        </div>
      </div>

      {error && <p className="has-text-danger">{error}</p>}

      {data && (
        <div className="columns mt-3">
          <div className="column">
            <p><strong>Current Savings:</strong> ₹{(data.current_savings ?? 0).toFixed(0)}</p>
            <p><strong>Monthly Savings:</strong> ₹{(data.monthly_savings ?? 0).toFixed(0)}</p>
          </div>
          <div className="column">
            <p><strong>Target Amount:</strong> ₹{(data.target_amount ?? 0).toFixed(0)}</p>
            <p>
              <strong>Months to Goal:</strong>{" "}
              {data.months_to_goal < 0
                ? "Not achievable at current rate"
                : `${(data.months_to_goal ?? 0).toFixed(1)} months`}
            </p>
          </div>
        </div>
      )}
    </div>
  );
}