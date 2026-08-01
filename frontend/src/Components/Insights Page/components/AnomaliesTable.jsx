import PropTypes from "prop-types";

export default function AnomaliesTable({ data }) {
  if (!data || !data.length) {
    return (
      <div className="box has-text-centered py-6">
        <span className="icon is-large has-text-success mb-2">
          <i className="fas fa-check-circle fa-3x" />
        </span>
        <h3 className="title is-5 mb-1">No Anomalies Detected</h3>
        <p className="has-text-grey">
          Your spending pattern is within normal expected ranges. 🎯
        </p>
      </div>
    );
  }

  // Helper function to turn Z-scores into human-friendly badges
  const getSeverityBadge = (zscore = 0) => {
    const absZ = Math.abs(zscore);

    if (absZ >= 4.0) {
      return <span className="tag is-danger is-light">Extreme Anomaly</span>;
    } else if (absZ >= 3.0) {
      return <span className="tag is-warning is-light">High Anomaly</span>;
    }
    return <span className="tag is-info is-light">Moderate Anomaly</span>;
  };

  return (
    <div className="box p-4">
      {/* Table Header Context */}
      <div className="level mb-4">
        <div className="level-left">
          <div>
            <h3 className="title is-5 mb-1">
              <span className="mr-2">⚠️</span> Flagged Transaction Anomalies
            </h3>
            <p className="subtitle is-7 has-text-grey">
              Transactions that deviate significantly from your usual spending behavior
            </p>
          </div>
        </div>
        <div className="level-right">
          <span className="tag is-warning is-medium is-rounded">
            {data.length} {data.length === 1 ? "Issue" : "Issues"} Found
          </span>
        </div>
      </div>

      {/* Table Content */}
      <div className="table-container">
        <table className="table is-fullwidth is-hoverable is-vcentered">
          <thead>
            <tr>
              <th>Date & Time</th>
              <th className="has-text-right">Amount</th>
              <th>Severity</th>
              <th className="has-text-right">
                <span
                  className="has-tooltip-multiline"
                  title="Z-Score measures how many standard deviations an amount is away from your normal mean spending."
                >
                  Deviation Score ℹ️
                </span>
              </th>
            </tr>
          </thead>
          <tbody>
            {data.map((a, i) => (
              <tr key={i}>
                <td>
                  {a.datetime ? (
                    <div>
                      <span className="is-block has-text-weight-semibold">
                        {new Date(a.datetime).toLocaleDateString(undefined, {
                          year: "numeric",
                          month: "short",
                          day: "numeric",
                        })}
                      </span>
                      <span className="is-size-7 has-text-grey">
                        {new Date(a.datetime).toLocaleTimeString([], {
                          hour: "2-digit",
                          minute: "2-digit",
                        })}
                      </span>
                    </div>
                  ) : (
                    "—"
                  )}
                </td>
                <td className="has-text-right has-text-weight-bold is-family-monospace">
                  ₹{(a.price ?? 0).toLocaleString("en-IN", {
                    minimumFractionDigits: 2,
                    maximumFractionDigits: 2,
                  })}
                </td>
                <td>{getSeverityBadge(a.zscore)}</td>
                <td className="has-text-right is-family-monospace has-text-grey-dark">
                  {(a.zscore ?? 0).toFixed(2)}σ
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}

AnomaliesTable.propTypes = {
  data: PropTypes.arrayOf(
    PropTypes.shape({
      datetime: PropTypes.string,
      price: PropTypes.number,
      zscore: PropTypes.number,
    })
  ),
};

AnomaliesTable.defaultProps = {
  data: [],
};
