import Flatpickr from "react-flatpickr";
import "flatpickr/dist/themes/light.css";
import PropTypes from "prop-types";

export default function FiltersPanel({
  mode, setMode, startDate, setStartDate,
  endDate, setEndDate, type, setType,
  keyword, setKeyword, trendMode, setTrendMode
}) {
  return (
    <div className="box">
      <h2 className="subtitle has-text-white mb-3">Controls & Filters</h2>
      <div className="columns is-multiline is-variable is-1">
        {/* View Mode */}
        <div className="column is-3">
          <label className="label has-text-light">View Mode</label>
          <div className="select is-fullwidth">
            <select value={mode} onChange={e => setMode(e.target.value)}>
              <option value="daily">Daily</option>
              <option value="weekly">Weekly</option>
              <option value="monthly">Monthly</option>
              <option value="lifetime">Lifetime</option>
              <option value="max">Max Transaction</option>
              <option value="min">Min Transaction</option>
            </select>
          </div>
        </div>

        {/* Dates */}
        <div className="column is-3">
          <label className="label has-text-light">Start Date</label>
          <Flatpickr options={{ dateFormat: "Y-m-d" }} value={startDate} onChange={([d]) => setStartDate(d)} className="input" placeholder="Start Date" />
        </div>
        <div className="column is-3">
          <label className="label has-text-light">End Date</label>
          <Flatpickr options={{ dateFormat: "Y-m-d" }} value={endDate} onChange={([d]) => setEndDate(d)} className="input" placeholder="End Date" />
        </div>

        {/* Type */}
        <div className="column is-3">
          <label className="label has-text-light">Type</label>
          <div className="select is-fullwidth">
            <select value={type} onChange={e => setType(e.target.value)}>
              <option value="all">All</option>
              <option value="income">Income</option>
              <option value="expense">Expense</option>
            </select>
          </div>
        </div>

        {/* Keyword */}
        <div className="column is-6">
          <label className="label has-text-light">Search</label>
          <input type="text" className="input" placeholder="Keyword..." value={keyword} onChange={e => setKeyword(e.target.value)} />
        </div>

        {/* Trend */}
        <div className="column is-6">
          <label className="label has-text-light">Trend Range</label>
          <div className="buttons are-small">
            <button className={`button ${trendMode === "6months" ? "is-link" : "is-light"}`} onClick={() => setTrendMode("6months")}>Last 6 Months</button>
            <button className={`button ${trendMode === "12weeks" ? "is-link" : "is-light"}`} onClick={() => setTrendMode("12weeks")}>Last 12 Weeks</button>
          </div>
        </div>
      </div>
    </div>
  );
}

FiltersPanel.propTypes = {
  mode: PropTypes.string.isRequired,
  setMode: PropTypes.func.isRequired,
  type: PropTypes.string.isRequired,
  setType: PropTypes.func.isRequired,
  keyword: PropTypes.string.isRequired,
  setKeyword: PropTypes.func.isRequired,
  startDate: PropTypes.instanceOf(Date),
  setStartDate: PropTypes.func.isRequired,
  endDate: PropTypes.instanceOf(Date),
  setEndDate: PropTypes.func.isRequired,
  trendMode: PropTypes.string.isRequired,
  setTrendMode: PropTypes.func.isRequired,
};