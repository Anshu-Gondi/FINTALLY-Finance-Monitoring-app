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

/* ---------------------------------------------------
   Utils
--------------------------------------------------- */

function useDebounce(value, delay = 500) {
  const [debounced, setDebounced] = useState(value);
  useEffect(() => {
    const t = setTimeout(() => setDebounced(value), delay);
    return () => clearTimeout(t);
  }, [value, delay]);
  return debounced;
}

async function fetchGraphQL(query, variables, token) {
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

/* ---------------------------------------------------
   Component
--------------------------------------------------- */

export default function InsightsPage() {
  const token = localStorage.getItem("token");

  /* ---------- UI State ---------- */
  const [mode, setMode] = useState(localStorage.getItem("insightsMode") || "monthly");
  const [trendMode, setTrendMode] = useState(localStorage.getItem("insightsTrend") || "6months");
  const [type, setType] = useState(localStorage.getItem("insightsType") || "all");

  const [startDate, setStartDate] = useState(
    localStorage.getItem("insightsStart")
      ? new Date(localStorage.getItem("insightsStart"))
      : null
  );
  const [endDate, setEndDate] = useState(
    localStorage.getItem("insightsEnd")
      ? new Date(localStorage.getItem("insightsEnd"))
      : null
  );

  const [keyword, setKeyword] = useState(localStorage.getItem("insightsKeyword") || "");
  const debouncedKeyword = useDebounce(keyword);

  /* ---------- Data State ---------- */
  const [barData, setBarData] = useState([]);
  const [categoryData, setCategoryData] = useState([]);
  const [trendData, setTrendData] = useState([]);

  const [budgetRisk, setBudgetRisk] = useState(null);
  const [emiRisk, setEmiRisk] = useState(null);
  const [cashflowForecast, setCashflowForecast] = useState([]);
  const [anomalies, setAnomalies] = useState([]);

  /* ---------- Loading / Error ---------- */
  const [loading, setLoading] = useState(false);
  const [loadingTrend, setLoadingTrend] = useState(false);
  const [loadingAdvanced, setLoadingAdvanced] = useState(false);
  const [error, setError] = useState(null);

  /* ---------- Persist UI ---------- */
  useEffect(() => {
    localStorage.setItem("insightsMode", mode);
    localStorage.setItem("insightsTrend", trendMode);
    localStorage.setItem("insightsType", type);
    localStorage.setItem("insightsKeyword", keyword);
    localStorage.setItem("insightsStart", startDate?.toISOString() || "");
    localStorage.setItem("insightsEnd", endDate?.toISOString() || "");
  }, [mode, trendMode, type, keyword, startDate, endDate]);

  /* ---------------------------------------------------
     BAR + CATEGORY DATA
  --------------------------------------------------- */
  useEffect(() => {
    if (!token) {
      setError("Missing auth token");
      return;
    }

    if (startDate && endDate && startDate > endDate) {
      setError("Start date cannot be after end date");
      return;
    }

    const run = async () => {
      setLoading(true);
      setError(null);

      try {
        let barQuery = "";
        let barVars = {};

        if (mode === "daily") {
          barQuery = `
            query ($interval: Int!) {
              dailySummary(interval: $interval) {
                data { period income expense total }
              }
            }
          `;
          barVars.interval = 1;
        }

        if (mode === "weekly" || mode === "monthly") {
          barQuery = `
            query ($range: String!) {
              periodSummary(range: $range) {
                data { period income expense total }
              }
            }
          `;
          barVars.range = mode;
        }

        if (mode === "lifetime") {
          barQuery = `
            query {
              lifetimeAnalysis {
                data { period income expense total }
              }
            }
          `;
        }

        if (mode === "max" || mode === "min") {
          barQuery = `
            query {
              minMaxTransaction {
                data { period income expense total }
              }
            }
          `;
        }

        const categoryQuery = `
          query ($start: String, $end: String, $type: String, $keyword: String) {
            categorySummary(
              start: $start
              end: $end
              type: $type
              keyword: $keyword
            ) {
              data { category total count }
            }
          }
        `;

        const [barRes, catRes] = await Promise.all([
          fetchGraphQL(barQuery, barVars, token),
          fetchGraphQL(
            categoryQuery,
            {
              start: startDate?.toISOString(),
              end: endDate?.toISOString(),
              type,
              keyword: debouncedKeyword || null,
            },
            token
          ),
        ]);

        let bars = [];
        if (mode === "daily") bars = barRes.dailySummary.data;
        if (mode === "weekly" || mode === "monthly") bars = barRes.periodSummary.data;
        if (mode === "lifetime") bars = barRes.lifetimeAnalysis.data;
        if (mode === "max" || mode === "min") {
          const all = barRes.minMaxTransaction.data || [];
          bars = mode === "max" ? all.filter(x => x.total > 0) : all.filter(x => x.total < 0);
        }

        setBarData(bars || []);
        setCategoryData(catRes.categorySummary.data || []);

      } catch (e) {
        console.error(e);
        setError(e.message);
      } finally {
        setLoading(false);
      }
    };

    run();
  }, [mode, startDate, endDate, type, debouncedKeyword, token]);

  /* ---------------------------------------------------
     TREND DATA
  --------------------------------------------------- */
  useEffect(() => {
    if (!token) return;

    setLoadingTrend(true);

    fetchGraphQL(
      `
      query ($range: String!) {
        trendSummary(range: $range) {
          data { period income expense total }
        }
      }
      `,
      { range: trendMode },
      token
    )
      .then(res => setTrendData(res.trendSummary.data || []))
      .catch(console.error)
      .finally(() => setLoadingTrend(false));
  }, [trendMode, token]);

  /* ---------------------------------------------------
     ADVANCED ANALYTICS
  --------------------------------------------------- */
  useEffect(() => {
    if (!token) return;

    setLoadingAdvanced(true);

    fetchGraphQL(
      `
      query ($budget: Float!, $end: String!) {
        budgetBreachPrediction(
          budgetAmount: $budget
          endDate: $end
        ) {
          breachProbability
          expectedSpend
          p95DaysToBreach
        }

        emiPressure {
          monthlyEmi
          survivabilityScore
          riskLevel
        }

        cashflowForecast(horizons: [30, 60, 90]) {
          points {
            horizonDays
            expectedBalance
          }
        }

        recurringAnomalies {
          description
          severity
          deviationPercent
        }
      }
      `,
      {
        budget: 15000,
        end: new Date().toISOString().slice(0, 10),
      },
      token
    )
      .then(res => {
        setBudgetRisk(res.budgetBreachPrediction);
        setEmiRisk(res.emiPressure);
        setCashflowForecast(res.cashflowForecast.points || []);
        setAnomalies(res.recurringAnomalies || []);
      })
      .catch(console.error)
      .finally(() => setLoadingAdvanced(false));
  }, [token]);



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
            
            {/* Advanced Analytics Sections */}
            
            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">
                  Budget Risk (Monte Carlo)
                </h2>

                {budgetRisk ? (
                  <div className="content has-text-white">
                    <p>
                      <strong>Probability of Breach:</strong>{" "}
                      {(budgetRisk.breachProbability * 100).toFixed(1)}%
                    </p>
                    <p>
                      <strong>Expected Spend:</strong> ₹{budgetRisk.expectedSpend.toFixed(0)}
                    </p>
                    <p>
                      <strong>P95 Days to Breach:</strong>{" "}
                      {budgetRisk.p95DaysToBreach ?? "Safe"}
                    </p>
                  </div>
                ) : (
                  <p className="has-text-grey">
                    {loadingAdvanced ? "Running simulations…" : "No data"}
                  </p>
                )}
              </div>
            </div>

            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">EMI Pressure</h2>

                {emiRisk ? (
                  <div className="columns has-text-white">
                    <div className="column">
                      <p><strong>Monthly EMI:</strong> ₹{emiRisk.monthlyEmi.toFixed(0)}</p>
                    </div>
                    <div className="column">
                      <p><strong>Survivability Score:</strong> {emiRisk.survivabilityScore.toFixed(1)}</p>
                      <p>
                        <strong>Risk Level:</strong>{" "}
                        <span className={`tag is-${emiRisk.riskLevel === "HIGH" ? "danger" : emiRisk.riskLevel === "MEDIUM" ? "warning" : "success"}`}>
                          {emiRisk.riskLevel}
                        </span>
                      </p>
                    </div>
                  </div>
                ) : (
                  <p className="has-text-grey">No EMI data</p>
                )}
              </div>
            </div>

            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">Cashflow Forecast</h2>

                {cashflowForecast.length ? (
                  <ResponsiveContainer width="100%" height={300}>
                    <LineChart data={cashflowForecast}>
                      <CartesianGrid strokeDasharray="3 3" />
                      <XAxis dataKey="horizonDays" />
                      <YAxis />
                      <Tooltip />
                      <Legend />
                      <Line
                        type="monotone"
                        dataKey="expectedBalance"
                        stroke="#3273dc"
                        name="Expected Balance"
                      />
                    </LineChart>
                  </ResponsiveContainer>
                ) : (
                  <p className="has-text-grey">Forecast unavailable</p>
                )}
              </div>
            </div>

            <div className="column is-12">
              <div className="box">
                <h2 className="subtitle has-text-white">Recurring Anomalies</h2>

                {anomalies.length ? (
                  <table className="table is-fullwidth is-striped is-dark">
                    <thead>
                      <tr>
                        <th>Description</th>
                        <th>Deviation %</th>
                        <th>Severity</th>
                      </tr>
                    </thead>
                    <tbody>
                      {anomalies.map((a, i) => (
                        <tr key={i}>
                          <td>{a.description}</td>
                          <td>{a.deviationPercent.toFixed(1)}%</td>
                          <td>{a.severity.toFixed(2)}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                ) : (
                  <p className="has-text-grey">No anomalies detected 🎯</p>
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
