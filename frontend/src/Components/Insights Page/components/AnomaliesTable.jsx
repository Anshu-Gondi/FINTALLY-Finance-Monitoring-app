import PropTypes from "prop-types";

// AnomaliesTable.jsx — API returns recurring anomalies with description/severity/deviation_percent
// Transaction anomalies have datetime/price/zscore
// Your hook pulls from anom.value?.anomalies which is the transaction anomaly list
export default function AnomaliesTable({ data }) {
  if (!data || !data.length) return <p>No anomalies detected 🎯</p>;

  return (
    <table className="table is-fullwidth is-striped">
      <thead>
        <tr>
          <th>Date</th>
          <th>Price</th>
          <th>Z-Score</th>
        </tr>
      </thead>
      <tbody>
        {data.map((a, i) => (
          <tr key={i}>
            <td>{a.datetime ?? "—"}</td>
            <td>₹{(a.price ?? 0).toFixed(2)}</td>
            <td>{(a.zscore ?? 0).toFixed(2)}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

AnomaliesTable.propTypes = {
  data: PropTypes.arrayOf(
    PropTypes.shape({
      description: PropTypes.string,
      deviationPercent: PropTypes.number,
      severity: PropTypes.number,
    })
  ),
};

AnomaliesTable.defaultProps = {
  data: [],
};