import PropTypes from "prop-types";

// EMICard.jsx
export default function EMICard({ data }) {
  if (!data) return <p>No EMI data</p>;

  const risk = data.risk_level ?? "";

  return (
    <div className="box columns">
      <div className="column">
        <p>
          <strong>Monthly EMI:</strong>{" "}
          ₹{(data.monthly_emi ?? 0).toFixed(0)}
        </p>
        <p>
          <strong>EMI Ratio:</strong>{" "}
          {((data.emi_ratio ?? 0) * 100).toFixed(1)}%
        </p>
      </div>
      <div className="column">
        <p>
          <strong>Survivability Score:</strong>{" "}
          {(data.survivability_score ?? 0).toFixed(1)}
        </p>
        <p>
          <strong>Risk Level:</strong>{" "}
          <span
            className={`tag is-${
              risk === "Critical" ? "danger"
              : risk === "High Risk" ? "warning"
              : "success"
            }`}
          >
            {risk || "Unknown"}
          </span>
        </p>
      </div>
    </div>
  );
}

EMICard.propTypes = {
  data: PropTypes.shape({
    monthly_emi: PropTypes.number,
    survivability_score: PropTypes.number,
    risk_level: PropTypes.string,
    emi_ratio: PropTypes.number,
  }),
};

EMICard.defaultProps = {
  data: null,
};