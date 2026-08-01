import PropTypes from "prop-types";

export default function EMICard({ data }) {
  // Empty State
  if (!data) {
    return (
      <div className="box p-5 has-text-centered">
        <span className="icon is-large has-text-grey-light mb-2">
          <i className="fas fa-credit-card fa-2x" />
        </span>
        <p className="has-text-grey">No EMI or loan commitment data available.</p>
      </div>
    );
  }

  const risk = data.risk_level ?? "";
  const emiRatioPct = (data.emi_ratio ?? 0) * 100;
  const survivability = data.survivability_score ?? 0;

  // Determine Tag Color & Guidance Message based on Risk Level
  let tagColor = "is-success";
  let statusMessage = "Healthy EMI level well within safe financial limits.";

  if (risk === "Critical") {
    tagColor = "is-danger";
    statusMessage = "Critical debt load! EMI payments are overwhelming cashflow.";
  } else if (risk === "High Risk" || risk === "High") {
    tagColor = "is-warning";
    statusMessage = "High debt commitment. Consider debt restructuring or prepayments.";
  } else if (risk === "Moderate") {
    tagColor = "is-info";
    statusMessage = "Moderate debt load. Keep an eye on secondary expenses.";
  }

  return (
    <div className="box p-5">
      {/* Card Header */}
      <div className="level is-mobile mb-3">
        <div className="level-left">
          <h3 className="title is-5 mb-0">
            <span className="mr-2">💳</span> EMI & Debt Health
          </h3>
        </div>
        <div className="level-right">
          <span className={`tag ${tagColor} is-medium is-rounded has-text-weight-bold`}>
            {risk || "Normal"}
          </span>
        </div>
      </div>

      <p className="is-size-7 has-text-grey mb-4">{statusMessage}</p>

      {/* Metrics Grid */}
      <div className="columns is-multiline is-mobile mb-2">
        {/* Monthly EMI Total */}
        <div className="column is-6-mobile is-6-tablet">
          <div className="has-background-light p-3 is-radius-small">
            <p className="heading has-text-grey mb-1">Monthly EMI Obligation</p>
            <p className="title is-4 is-family-monospace has-text-dark">
              ₹
              {(data.monthly_emi ?? 0).toLocaleString("en-IN", {
                maximumFractionDigits: 0,
              })}
            </p>
          </div>
        </div>

        {/* Survivability Score */}
        <div className="column is-6-mobile is-6-tablet">
          <div className="has-background-light p-3 is-radius-small">
            <div className="is-flex is-justify-content-space-between mb-1">
              <p
                className="heading has-text-grey mb-0"
                title="Measures cash cushion remaining after paying monthly EMIs."
              >
                Survivability Score ℹ️
              </p>
            </div>
            <p className="title is-4 is-family-monospace has-text-dark mb-2">
              {survivability.toFixed(1)} <span className="is-size-7 has-text-grey">/ 10</span>
            </p>
            <progress
              className={`progress ${
                survivability > 7 ? "is-success" : survivability > 4 ? "is-warning" : "is-danger"
              } is-small`}
              value={survivability}
              max="10"
            >
              {survivability}/10
            </progress>
          </div>
        </div>
      </div>

      {/* EMI to Income Ratio Visual */}
      <div className="mt-3 pt-2 border-top">
        <div className="is-flex is-justify-content-space-between mb-1">
          <span
            className="is-size-7 has-text-weight-semibold"
            title="Percentage of income consumed by fixed EMI payments."
          >
            EMI to Income Ratio ℹ️
          </span>
          <span className="is-size-7 has-text-weight-bold">{emiRatioPct.toFixed(1)}%</span>
        </div>
        <progress
          className={`progress ${
            emiRatioPct > 50 ? "is-danger" : emiRatioPct > 35 ? "is-warning" : "is-success"
          } is-medium`}
          value={emiRatioPct}
          max="100"
        >
          {emiRatioPct.toFixed(1)}%
        </progress>
        <div className="is-flex is-justify-content-space-between is-size-7 has-text-grey-light">
          <span>0%</span>
          <span>Recommended Max: 35-40%</span>
          <span>100%</span>
        </div>
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
