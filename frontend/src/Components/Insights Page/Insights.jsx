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

const COLORS = ["#00d1b2", "#ff3860", "#ffdd57", "#3273dc", "#b86bff", "#ff8c00"];

// Debounce utility
function useDebounce(value, delay) {
  const [debounced, setDebounced] = useState(value);
  useEffect(() => {
    const handler = setTimeout(() => setDebounced(value), delay);
    return () => clearTimeout(handler);
  }, [value, delay]);
  return debounced;
}

// GraphQL helper
async function fetchGraphQL(query, variables = {}, token) {
  const res = await fetch(`${import.meta.env.VITE_DJANGO_URL}/graphql/`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${token}`,
    },
    body: JSON.stringify({ query, variables }),
  });
  const json = await res.json();
  if (json.errors) throw new Error(json.errors[0].message);
  return json.data;
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

  // Fetch GraphQL Insights
  useEffect(() => {
    if (!token) return setError("Authorization token missing. Please login again.");
    if (startDate && endDate && startDate > endDate) return setError("Start date cannot be after end date.");

    const fetchData = async () => {
      setLoading(true);
      setError(null);

      try {
        let query = "";
        let variables = {};

        if (mode === "max" || mode === "min") {
          query = `
            query {
              minMaxTransaction {
                data {
                  period
                  income
                  expense
                  total
                }
              }
            }
          `;
        } else if (mode === "daily" || mode === "weekly" || mode === "monthly" || mode === "lifetime") {
          query = `
            query PeriodSummary($range: String, $bucketDays: Int) {
              ${mode === "daily" ? "dailySummary(interval: $bucketDays)" : mode === "lifetime" ? "lifetimeAnalysis" : "periodSummary(range: $range, bucketDays: $bucketDays)"} {
                data { period income expense total }
              }
            }
          `;
          if (mode === "daily") variables.bucketDays = 1;
          else if (mode === "weekly") variables.range = "weekly";
          else if (mode === "monthly") variables.range = "monthly";
        }

        // Category query
        const categoryQuery = `
          query CategorySummary($start: String, $end: String, $type: String, $keyword: String, $limit: Int) {
            categorySummary(start: $start, end: $end, type: $type, keyword: $keyword, limit: $limit) {
              data { category total count }
            }
          }
        `;
        variables = { ...variables, start: startDate?.toISOString(), end: endDate?.toISOString(), type, keyword: debouncedKeyword };

        const [barRes, categoryRes] = await Promise.all([
          fetchGraphQL(query, variables, token),
          fetchGraphQL(categoryQuery, variables, token),
        ]);

        // Bar data
        let barResult = [];
        if (mode === "max" || mode === "min") {
          barResult = barRes.minMaxTransaction.data;
          if (mode === "min") barResult = barResult.filter(d => d.total === Math.min(...barResult.map(x => x.total)));
          else if (mode === "max") barResult = barResult.filter(d => d.total === Math.max(...barResult.map(x => x.total)));
        } else if (mode === "daily") barResult = barRes.dailySummary.data;
        else if (mode === "weekly" || mode === "monthly") barResult = barRes.periodSummary.data;
        else if (mode === "lifetime") barResult = barRes.lifetimeAnalysis.data;

        setBarData(barResult || []);
        setCategoryData(categoryRes.categorySummary.data || []);
      } catch (err) {
        console.error(err);
        setError(err.message || "Failed to fetch insights.");
      } finally {
        setLoading(false);
      }
    };

    fetchData();
  }, [mode, startDate, endDate, type, debouncedKeyword, token]);

  // Fetch trend data
  useEffect(() => {
    if (!token) return;
    setLoadingTrend(true);

    const fetchTrend = async () => {
      try {
        const query = `
          query TrendSummary($range: String!) {
            trendSummary(range: $range) {
              data { period income expense total }
            }
          }
        `;
        const variables = { range: trendMode };
        const res = await fetchGraphQL(query, variables, token);
        setTrendData(res.trendSummary.data || []);
      } catch (err) {
        console.error("Trend fetch error:", err);
      } finally {
        setLoadingTrend(false);
      }
    };

    fetchTrend();
  }, [trendMode, token]);

  return (
    <>
      <Navbar />
      <section className="section">
        <div className="container">
          <h1 className="title has-text-white">Financial Insights</h1>

          {/* Filters Box */}
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

              {/* Start & End Date */}
              <div className="column is-3">
                <label className="label has-text-light">Start Date</label>
                <Flatpickr
                  options={{ dateFormat: "Y-m-d" }}
                  value={startDate}
                  onChange={([date]) => setStartDate(date)}
                  className="input"
                  placeholder="Start Date"
                />
              </div>

              <div className="column is-3">
                <label className="label has-text-light">End Date</label>
                <Flatpickr
                  options={{ dateFormat: "Y-m-d" }}
                  value={endDate}
                  onChange={([date]) => setEndDate(date)}
                  className="input"
                  placeholder="End Date"
                />
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
                <input
                  type="text"
                  className="input"
                  placeholder="Keyword..."
                  value={keyword}
                  onChange={e => setKeyword(e.target.value)}
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

          {loading && <progress className="progress is-small is-info" max="100">Loading…</progress>}
          {error && <div className="notification is-danger">{error}</div>}

          {/* Bar Chart */}
          <div className="columns is-multiline">
            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">Bar Chart (Grouped Totals)</h2>
                {barData.length ? (
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
