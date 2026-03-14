import PropTypes from "prop-types";

export default function EMICard({ data }) {
  if (!data) return <p>No EMI data</p>;

  return (
    <div className="columns">
      <div className="column">
        <p><strong>Monthly EMI:</strong> ₹{data.monthlyEmi.toFixed(0)}</p>
      </div>
      <div className="column">
        <p><strong>Survivability Score:</strong> {data.survivabilityScore.toFixed(1)}</p>
        <p>
          <strong>Risk Level:</strong>{" "}
          <span className={`tag is-${data.riskLevel === "HIGH" ? "danger" : data.riskLevel === "MEDIUM" ? "warning" : "success"}`}>
            {data.riskLevel}
          </span>
        </p>
      </div>
    </div>
  );
}

EMICard.propTypes = {
  data: PropTypes.shape({
    monthlyEmi: PropTypes.number,
    survivabilityScore: PropTypes.number,
    riskLevel: PropTypes.string,
  }),
};

EMICard.defaultProps = {
  data: null,
};