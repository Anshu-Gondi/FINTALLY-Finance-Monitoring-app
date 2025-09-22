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
const COLORS = [
  "#00d1b2",
  "#ff3860",
  "#ffdd57",
  "#3273dc",
  "#b86bff",
  "#ff8c00",
];

function InsightsPage() {
  const [barData, setBarData] = useState([]);
  const [categoryData, setCategoryData] = useState([]);
  const [mode, setMode] = useState("monthly");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [startDate, setStartDate] = useState(null);
  const [endDate, setEndDate] = useState(null);
  const [type, setType] = useState("all");
  const [keyword, setKeyword] = useState("");
  const [trendMode, setTrendMode] = useState("6months");
  const [trendData, setTrendData] = useState([]);

  useEffect(() => {
    const fetchInsights = async () => {
      setLoading(true);
      setError(null);

      try {
        const queryParams = new URLSearchParams({
          start: startDate ? new Date(startDate).toISOString() : "",
          end: endDate ? new Date(endDate).toISOString() : "",
          type,
          keyword,
        });

        const [barRes, categoryRes] = await Promise.all([
          fetch(`${API_URL}/insights/${mode}?${queryParams}`, {
            credentials: "include",
            headers: {
              Authorization: `Bearer ${localStorage.getItem("token")}`,
            },
          }),
          fetch(`${API_URL}/insights/categories?${queryParams}`, {
            credentials: "include",
            headers: {
              Authorization: `Bearer ${localStorage.getItem("token")}`,
            },
          }),
        ]);

        if (!barRes.ok || !categoryRes.ok) {
          throw new Error("One or more insight endpoints failed.");
        }

        const barJson = await barRes.json();
        const categoryJson = await categoryRes.json();

        setBarData(barJson);
        setCategoryData(categoryJson.data || []);
      } catch (err) {
        console.error("Error fetching insights:", err);
        setError("Failed to load insights.");
      } finally {
        setLoading(false);
      }
    };

    fetchInsights();
  }, [mode, startDate, endDate, type, keyword]);

  // Fetch trend chart
  useEffect(() => {
    const fetchTrendData = async () => {
      try {
        const res = await fetch(
          `${API_URL}/insights/trends?range=${trendMode}`,
          {
            credentials: "include",
            headers: {
              Authorization: `Bearer ${localStorage.getItem("token")}`,
            },
          }
        );

        if (!res.ok) throw new Error("Trend fetch failed");

        const json = await res.json();
        setTrendData(json.data || []);
      } catch (err) {
        console.error("Trend error:", err);
      }
    };

    fetchTrendData();
  }, [trendMode]);

  return (
    <>
      <Navbar />
      <section className="section">
        <div className="container">
          <h1 className="title has-text-white">Financial Insights</h1>

          <div className="box">
            <h2 className="subtitle has-text-white has-text-light mb-3">Controls & Filters</h2>
            <div className="columns is-multiline is-variable is-1">
              {/* Mode Selector */}
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
                  onChange={([date]) => setEndDate(date)}
                  className="input"
                />
              </div>

              {/* Type Selector */}
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

              {/* Keyword Search */}
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

              {/* Trend Mode Buttons */}
              <div className="column is-6">
                <label className="label has-text-light">Trend Range</label>
                <div className="buttons are-small">
                  <button
                    className={`button ${trendMode === "6months" ? "is-link" : "is-light"
                      }`}
                    onClick={() => setTrendMode("6months")}
                  >
                    Last 6 Months
                  </button>
                  <button
                    className={`button ${trendMode === "12weeks" ? "is-link" : "is-light"
                      }`}
                    onClick={() => setTrendMode("12weeks")}
                  >
                    Last 12 Weeks
                  </button>
                </div>
              </div>
            </div>
          </div>

          {/* Loading & Error */}
          {loading && (
            <progress className="progress is-small is-info" max="100">
              Loading…
            </progress>
          )}
          {error && <div className="notification is-danger">{error}</div>}

          {/* Charts */}
          <div className="columns is-multiline">
            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">Bar Chart (Grouped Totals)</h2>
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
              </div>
            </div>

            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">Category Breakdown (Pie Chart)</h2>
                <ResponsiveContainer width="100%" height={300}>
                  <PieChart>
                    <Pie
                      data={categoryData}
                      dataKey="total"
                      nameKey="_id"
                      cx="50%"
                      cy="50%"
                      outerRadius={100}
                      label
                    >
                      {categoryData.map((entry, index) => (
                        <Cell
                          key={`cell-${index}`}
                          fill={COLORS[index % COLORS.length]}
                        />
                      ))}
                    </Pie>
                    <Tooltip />
                    <Legend />
                  </PieChart>
                </ResponsiveContainer>
              </div>
            </div>

            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">Income vs Expense Trend</h2>
                <ResponsiveContainer width="100%" height={300}>
                  <LineChart data={trendData}>
                    <CartesianGrid strokeDasharray="3 3" />
                    <XAxis dataKey="period" />
                    <YAxis />
                    <Tooltip />
                    <Legend />
                    <Line
                      type="monotone"
                      dataKey="income"
                      stroke="#48c774"
                      name="Income"
                    />
                    <Line
                      type="monotone"
                      dataKey="expense"
                      stroke="#ff3860"
                      name="Expense"
                    />
                  </LineChart>
                </ResponsiveContainer>
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
