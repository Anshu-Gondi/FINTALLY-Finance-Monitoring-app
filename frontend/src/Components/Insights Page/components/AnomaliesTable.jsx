import PropTypes from "prop-types";

export default function AnomaliesTable({ data }) {
  if (!data || !data.length) return <p>No anomalies detected 🎯</p>;

  return (
    <table className="table is-fullwidth is-striped is-dark">
      <thead>
        <tr>
          <th>Description</th>
          <th>Deviation %</th>
          <th>Severity</th>
        </tr>
      </thead>
      <tbody>
        {data.map((a, i) => (
          <tr key={i}>
            <td>{a.description}</td>
            <td>{a.deviationPercent.toFixed(1)}%</td>
            <td>{a.severity.toFixed(2)}</td>
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