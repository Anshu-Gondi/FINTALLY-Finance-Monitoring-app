import { useEffect, useState } from "react";
import {
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  PieChart,
  Pie,
  Cell,
  Legend,
  ResponsiveContainer,
  LineChart,
  Line,
} from "recharts";
import Flatpickr from "react-flatpickr";
import "flatpickr/dist/themes/light.css";
import "bulma/css/bulma.min.css";
import "./Insights.css";
import Navbar from "../../Shared Components/Navbar/Navbar";
import Footer from "../../Shared Components/Footer/Footer";

const API_URL = import.meta.env.VITE_API_URL;
const COLORS = ["#00d1b2", "#ff3860", "#ffdd57", "#3273dc", "#b86bff", "#ff8c00"];

// Utility to debounce rapid calls
function useDebounce(value, delay) {
  const [debounced, setDebounced] = useState(value);
  useEffect(() => {
    const handler = setTimeout(() => setDebounced(value), delay);
    return () => clearTimeout(handler);
  }, [value, delay]);
  return debounced;
}

function InsightsPage() {
  const [barData, setBarData] = useState([]);
  const [categoryData, setCategoryData] = useState([]);
  const [trendData, setTrendData] = useState([]);
  const [mode, setMode] = useState(localStorage.getItem("insightsMode") || "monthly");
  const [startDate, setStartDate] = useState(localStorage.getItem("insightsStart") ? new Date(localStorage.getItem("insightsStart")) : null);
  const [endDate, setEndDate] = useState(localStorage.getItem("insightsEnd") ? new Date(localStorage.getItem("insightsEnd")) : null);
  const [type, setType] = useState(localStorage.getItem("insightsType") || "all");
  const [keyword, setKeyword] = useState(localStorage.getItem("insightsKeyword") || "");
  const debouncedKeyword = useDebounce(keyword, 500);
  const [trendMode, setTrendMode] = useState(localStorage.getItem("insightsTrend") || "6months");

  const [loading, setLoading] = useState(false);
  const [loadingTrend, setLoadingTrend] = useState(false);
  const [error, setError] = useState(null);

  const token = localStorage.getItem("token");

  // Persist filter state
  useEffect(() => {
    localStorage.setItem("insightsMode", mode);
    localStorage.setItem("insightsStart", startDate ? startDate.toISOString() : "");
    localStorage.setItem("insightsEnd", endDate ? endDate.toISOString() : "");
    localStorage.setItem("insightsType", type);
    localStorage.setItem("insightsKeyword", keyword);
    localStorage.setItem("insightsTrend", trendMode);
  }, [mode, startDate, endDate, type, keyword, trendMode]);

  // Fetch bar & category data
  useEffect(() => {
    const fetchInsights = async () => {
      if (!token) return setError("Authorization token missing. Please login again.");
      if (startDate && endDate && startDate > endDate) return setError("Start date cannot be after end date.");

      setLoading(true);
      setError(null);

      try {
        const queryParams = new URLSearchParams();
        if (startDate) queryParams.append("start", startDate.toISOString());
        if (endDate) queryParams.append("end", endDate.toISOString());
        if (type !== "all") queryParams.append("type", type);
        if (debouncedKeyword) queryParams.append("keyword", debouncedKeyword);

        const [barRes, categoryRes] = await Promise.all([
          fetch(`${API_URL}/insights/${mode}?${queryParams}`, {
            credentials: "include",
            headers: { Authorization: `Bearer ${token}` },
          }),
          fetch(`${API_URL}/insights/categories?${queryParams}`, {
            credentials: "include",
            headers: { Authorization: `Bearer ${token}` },
          }),
        ]);

        if (!barRes.ok) throw new Error("Failed to load main chart data.");
        if (!categoryRes.ok) throw new Error("Failed to load category data.");

        const barJson = await barRes.json();
        const categoryJson = await categoryRes.json();

        setBarData(barJson.length ? barJson : []);
        setCategoryData(categoryJson.data?.length ? categoryJson.data : []);
      } catch (err) {
        console.error(err);
        setError(err.message || "Failed to fetch insights.");
      } finally {
        setLoading(false);
      }
    };

    fetchInsights();
  }, [mode, startDate, endDate, type, debouncedKeyword, token]);

  // Fetch trend chart
  useEffect(() => {
    const fetchTrendData = async () => {
      if (!token) return;
      setLoadingTrend(true);
      try {
        const res = await fetch(`${API_URL}/insights/trends?range=${trendMode}`, {
          credentials: "include",
          headers: { Authorization: `Bearer ${token}` },
        });
        if (!res.ok) throw new Error("Trend data fetch failed");
        const json = await res.json();
        setTrendData(json.data?.length ? json.data : []);
      } catch (err) {
        console.error("Trend error:", err);
      } finally {
        setLoadingTrend(false);
      }
    };
    fetchTrendData();
  }, [trendMode, token]);

  return (
    <>
      <Navbar />
      <section className="section">
        <div className="container">
          <h1 className="title has-text-white">Financial Insights</h1>

          <div className="box">
            <h2 className="subtitle has-text-white has-text-light mb-3">Controls & Filters</h2>
            <div className="columns is-multiline is-variable is-1">
              {/* View Mode */}
              <div className="column is-3">
                <label className="label has-text-light">View Mode</label>
                <div className="select is-fullwidth">
                  <select value={mode} onChange={(e) => setMode(e.target.value)}>
                    <option value="daily">Daily</option>
                    <option value="weekly">Weekly</option>
                    <option value="monthly">Monthly</option>
                    <option value="lifetime">Lifetime</option>
                    <option value="max">Max Transaction</option>
                    <option value="min">Min Transaction</option>
                  </select>
                </div>
              </div>

              {/* Start Date */}
              <div className="column is-3">
                <label className="label has-text-light">Start Date</label>
                <Flatpickr
                  options={{ dateFormat: "Y-m-d" }}
                  placeholder="Start Date"
                  value={startDate}
                  onChange={([date]) => setStartDate(date)}
                  className="input"
                />
              </div>

              {/* End Date */}
              <div className="column is-3">
                <label className="label has-text-light">End Date</label>
                <Flatpickr
                  options={{ dateFormat: "Y-m-d" }}
                  placeholder="End Date"
                  value={endDate}
                  onChange={([date]) => setEndDate(date)}
                  className="input"
                />
              </div>

              {/* Type */}
              <div className="column is-3">
                <label className="label has-text-light">Type</label>
                <div className="select is-fullwidth">
                  <select value={type} onChange={(e) => setType(e.target.value)}>
                    <option value="all">All</option>
                    <option value="income">Income</option>
                    <option value="expense">Expense</option>
                  </select>
                </div>
              </div>

              {/* Keyword */}
              <div className="column is-6">
                <label className="label has-text-light">Search</label>
                <input
                  className="input"
                  type="text"
                  placeholder="Keyword..."
                  value={keyword}
                  onChange={(e) => setKeyword(e.target.value)}
                />
              </div>

              {/* Trend Range */}
              <div className="column is-6">
                <label className="label has-text-light">Trend Range</label>
                <div className="buttons are-small">
                  <button className={`button ${trendMode === "6months" ? "is-link" : "is-light"}`} onClick={() => setTrendMode("6months")}>Last 6 Months</button>
                  <button className={`button ${trendMode === "12weeks" ? "is-link" : "is-light"}`} onClick={() => setTrendMode("12weeks")}>Last 12 Weeks</button>
                </div>
              </div>
            </div>
          </div>

          {/* Global Loading & Errors */}
          {loading && <progress className="progress is-small is-info" max="100">Loading…</progress>}
          {error && <div className="notification is-danger">{error}</div>}

          {/* Bar Chart */}
          <div className="columns is-multiline">
            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">Bar Chart (Grouped Totals)</h2>
                {loading ? (
                  <progress className="progress is-small is-info" max="100">Loading…</progress>
                ) : barData.length ? (
                  <ResponsiveContainer width="100%" height={300}>
                    <BarChart data={barData}>
                      <CartesianGrid strokeDasharray="3 3" />
                      <XAxis dataKey="period" />
                      <YAxis />
                      <Tooltip />
                      <Legend />
                      <Bar dataKey="total" fill="#00d1b2" name="Total" />
                      <Bar dataKey="income" fill="#48c774" name="Income" />
                      <Bar dataKey="expense" fill="#ff3860" name="Expense" />
                    </BarChart>
                  </ResponsiveContainer>
                ) : (
                  <p className="has-text-white">No data available for selected filters.</p>
                )}
              </div>
            </div>

            {/* Pie Chart */}
            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">Category Breakdown (Pie Chart)</h2>
                {categoryData.length ? (
                  <ResponsiveContainer width="100%" height={300}>
                    <PieChart>
                      <Pie data={categoryData} dataKey="total" nameKey="category" cx="50%" cy="50%" outerRadius={100} label>
                        {categoryData.map((entry, index) => (
                          <Cell key={`cell-${index}`} fill={COLORS[index % COLORS.length]} />
                        ))}
                      </Pie>
                      <Tooltip />
                      <Legend />
                    </PieChart>
                  </ResponsiveContainer>
                ) : (
                  <p className="has-text-white">No category data available.</p>
                )}
              </div>
            </div>

            {/* Trend Chart */}
            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">Income vs Expense Trend</h2>
                {loadingTrend ? (
                  <progress className="progress is-small is-info" max="100">Loading…</progress>
                ) : trendData.length ? (
                  <ResponsiveContainer width="100%" height={300}>
                    <LineChart data={trendData}>
                      <CartesianGrid strokeDasharray="3 3" />
                      <XAxis dataKey="period" />
                      <YAxis />
                      <Tooltip />
                      <Legend />
                      <Line type="monotone" dataKey="income" stroke="#48c774" name="Income" />
                      <Line type="monotone" dataKey="expense" stroke="#ff3860" name="Expense" />
                    </LineChart>
                  </ResponsiveContainer>
                ) : (
                  <p className="has-text-white">No trend data available.</p>
                )}
              </div>
            </div>
          </div>
        </div>
      </section>
      <Footer />
    </>
  );
}

export default InsightsPage;
