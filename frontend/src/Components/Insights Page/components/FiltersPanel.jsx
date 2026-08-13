import Flatpickr from "react-flatpickr";
import "flatpickr/dist/themes/light.css";
import PropTypes from "prop-types";

export default function FiltersPanel({
  mode,
  setMode,
  startDate,
  setStartDate,
  endDate,
  setEndDate,
  type,
  setType,
  keyword,
  setKeyword,
  trendMode,
  setTrendMode,
}) {
  // Quick helper to reset all filter controls
  const handleResetFilters = () => {
    setMode("monthly");
    setStartDate(null);
    setEndDate(null);
    setType("all");
    setKeyword("");
    setTrendMode("6months");
  };

  // Quick Date Preset Handler
  const applyDatePreset = (preset) => {
    const end = new Date();
    const start = new Date();

    if (preset === "thisMonth") {
      start.setDate(1);
    } else if (preset === "last30") {
      start.setDate(end.getDate() - 30);
    } else if (preset === "clear") {
      setStartDate(null);
      setEndDate(null);
      return;
    }

    setStartDate(start);
    setEndDate(end);
  };

  const hasActiveFilters =
    mode !== "monthly" ||
    startDate !== null ||
    endDate !== null ||
    type !== "all" ||
    keyword !== "" ||
    trendMode !== "6months";

  return (
    <div className="box p-4">
      {/* Header with Title & Reset Button */}
      <div className="level is-mobile mb-3">
        <div className="level-left">
          <h2 className="title is-5 mb-0">
            <span className="mr-2">🎛️</span> Controls & Filters
          </h2>
        </div>
        <div className="level-right">
          {hasActiveFilters && (
            <button
              className="button is-small is-ghost has-text-danger"
              onClick={handleResetFilters}
            >
              <span className="icon is-small">
                <i className="fas fa-undo" />
              </span>
              <span>Reset Filters</span>
            </button>
          )}
        </div>
      </div>

      <div className="columns is-multiline is-variable is-2">
        {/* 1. View Mode */}
        <div className="column is-12-mobile is-6-tablet is-3-desktop">
          <label className="label is-small has-text-grey-dark mb-1">
            View Mode
          </label>
          <div className="control has-icons-left">
            <div className="select is-fullwidth is-small">
              <select value={mode} onChange={(e) => setMode(e.target.value)}>
                <option value="daily">Daily Summary</option>
                <option value="weekly">Weekly Summary</option>
                <option value="monthly">Monthly Summary</option>
                <option value="lifetime">Lifetime</option>
                <option value="max">Max Transaction</option>
                <option value="min">Min Transaction</option>
              </select>
            </div>
            <span className="icon is-small is-left">
              <i className="fas fa-layer-group" />
            </span>
          </div>
        </div>

        {/* 2. Transaction Type */}
        <div className="column is-12-mobile is-6-tablet is-3-desktop">
          <label className="label is-small has-text-grey-dark mb-1">
            Transaction Type
          </label>
          <div className="control has-icons-left">
            <div className="select is-fullwidth is-small">
              <select value={type} onChange={(e) => setType(e.target.value)}>
                <option value="all">All Transactions</option>
                <option value="income">Income Only</option>
                <option value="expense">Expense Only</option>
              </select>
            </div>
            <span className="icon is-small is-left">
              <i className="fas fa-filter" />
            </span>
          </div>
        </div>

        {/* 3. Search Keyword */}
        <div className="column is-12-mobile is-12-tablet is-6-desktop">
          <label className="label is-small has-text-grey-dark mb-1">
            Search Keyword
          </label>
          <div className="control has-icons-left has-icons-right">
            <input
              type="text"
              className="input is-small"
              placeholder="Search category, note, merchant..."
              value={keyword}
              onChange={(e) => setKeyword(e.target.value)}
            />
            <span className="icon is-small is-left">
              <i className="fas fa-search" />
            </span>
            {keyword && (
              <span
                className="icon is-small is-right style-clickable"
                onClick={() => setKeyword("")}
                style={{ pointerEvents: "all", cursor: "pointer" }}
              >
                <i className="fas fa-times-circle has-text-grey-light" />
              </span>
            )}
          </div>
        </div>

        {/* 4. Start Date */}
        <div className="column is-12-mobile is-6-tablet is-3-desktop">
          <label className="label is-small has-text-grey-dark mb-1">
            Start Date
          </label>
          <div className="control has-icons-left">
            <Flatpickr
              options={{ dateFormat: "Y-m-d", allowInput: true }}
              value={startDate || ""}
              onChange={([selectedDate]) => setStartDate(selectedDate || null)}
              onClear={() => setStartDate(null)}
              className="input is-small"
              placeholder="YYYY-MM-DD"
            />
            <span className="icon is-small is-left">
              <i className="fas fa-calendar-alt" />
            </span>
          </div>
        </div>

        {/* 5. End Date */}
        <div className="column is-12-mobile is-6-tablet is-3-desktop">
          <label className="label is-small has-text-grey-dark mb-1">
            End Date
          </label>
          <div className="control has-icons-left">
            <Flatpickr
              options={{ dateFormat: "Y-m-d", allowInput: true }}
              value={endDate || ""}
              onChange={([selectedDate]) => setEndDate(selectedDate || null)}
              onClear={() => setEndDate(null)}
              className="input is-small"
              placeholder="YYYY-MM-DD"
            />
            <span className="icon is-small is-left">
              <i className="fas fa-calendar-check" />
            </span>
          </div>
        </div>

        {/* 6. Date Quick Presets & Trend Range Selector */}
        <div className="column is-12-mobile is-12-tablet is-6-desktop">
          <div className="columns is-mobile is-variable is-1">
            {/* Quick Date Shortcuts */}
            <div className="column is-6">
              <label className="label is-small has-text-grey-dark mb-1">
                Date Presets
              </label>
              <div className="buttons are-small mb-0">
                <button
                  type="button"
                  className="button is-light"
                  onClick={() => applyDatePreset("thisMonth")}
                >
                  This Month
                </button>
                <button
                  type="button"
                  className="button is-light"
                  onClick={() => applyDatePreset("last30")}
                >
                  Last 30D
                </button>
              </div>
            </div>

            {/* Trend Granularity Selection */}
            <div className="column is-6">
              <label className="label is-small has-text-grey-dark mb-1">
                Trend Mode
              </label>
              <div className="buttons are-small mb-0">
                <button
                  type="button"
                  className={`button ${
                    trendMode === "6months" ? "is-link" : "is-light"
                  }`}
                  onClick={() => setTrendMode("6months")}
                >
                  6 Months
                </button>
                <button
                  type="button"
                  className={`button ${
                    trendMode === "12weeks" ? "is-link" : "is-light"
                  }`}
                  onClick={() => setTrendMode("12weeks")}
                >
                  12 Weeks
                </button>
              </div>
            </div>
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
  startDate: PropTypes.oneOfType([PropTypes.instanceOf(Date), PropTypes.string]),
  setStartDate: PropTypes.func.isRequired,
  endDate: PropTypes.oneOfType([PropTypes.instanceOf(Date), PropTypes.string]),
  setEndDate: PropTypes.func.isRequired,
  trendMode: PropTypes.string.isRequired,
  setTrendMode: PropTypes.func.isRequired,
};
