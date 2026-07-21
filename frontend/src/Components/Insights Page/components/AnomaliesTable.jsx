import PropTypes from "prop-types";

export default function AnomaliesTable({ data }) {
  if (!data || !data.length) return <p className="has-text-grey">No anomalies detected 🎯</p>;

  return (
    <div className="table-container">
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
              <td>{a.datetime ? new Date(a.datetime).toLocaleDateString() : "—"}</td>
              <td>₹{(a.price ?? 0).toFixed(2)}</td>
              <td>{(a.zscore ?? 0).toFixed(2)}</td>
            </tr>
          ))}
        </tbody>
      </table>
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
