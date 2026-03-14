import PropTypes from "prop-types";

export default function SavingsCard({ data }) {
  if (!data) return <p>No savings data</p>;

  return (
    <div>
      <p><strong>Total Saved:</strong> ₹{data.total}</p>
      <p><strong>Monthly Average:</strong> ₹{data.monthlyAverage}</p>
    </div>
  );
}

SavingsCard.propTypes = {
  data: PropTypes.shape({
    total: PropTypes.number,
    monthlyAverage: PropTypes.number,
  }),
};

SavingsCard.defaultProps = {
  data: null,
};