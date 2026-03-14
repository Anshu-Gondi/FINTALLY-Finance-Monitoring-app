import PropTypes from "prop-types";

export default function GoalCard({ data }) {
  if (!data) return <p>No goal data</p>;

  return (
    <div>
      <p><strong>Goal:</strong> {data.name}</p>
      <p><strong>Target:</strong> ₹{data.target}</p>
      <p><strong>Progress:</strong> {data.progress}%</p>
    </div>
  );
}

GoalCard.propTypes = {
  data: PropTypes.shape({
    name: PropTypes.string,
    target: PropTypes.number,
    progress: PropTypes.number,
  }),
};

GoalCard.defaultProps = {
  data: null,
};