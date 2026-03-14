import Navbar from "../../Shared Components/Navbar/Navbar";
import Footer from "../../Shared Components/Footer/Footer";
import {
  GoalCard,
  FiltersPanel,
  PieChartBlock,
  TrendChartBlock,
  SavingsCard,
  BarChartBlock,
  AnomaliesTable,
  BudgetBreachCard,
  EMICard,
  CashflowChartBlock
} from "./components";

import usePersistedInsightsState from "./hooks/usePersistedInsightsState";
import useBarAndCategoryData from "./hooks/useBarAndCategoryData";
import useAdvancedAnalytics from "./hooks/useAdvancedAnalytics";

export default function InsightsPage() {
  const token = localStorage.getItem("token");

  // Persisted UI state
  const [mode, setMode] = usePersistedInsightsState("insightsMode", "monthly");
  const [trendMode, setTrendMode] = usePersistedInsightsState("insightsTrend", "6months");
  const [type, setType] = usePersistedInsightsState("insightsType", "all");
  const [startDate, setStartDate] = usePersistedInsightsState("insightsStart", null);
  const [endDate, setEndDate] = usePersistedInsightsState("insightsEnd", null);
  const [keyword, setKeyword] = usePersistedInsightsState("insightsKeyword", "");

  // Data hooks
  const { barData, categoryData, loading: loadingBar, error: errorBar } = useBarAndCategoryData({
    mode, startDate, endDate, type, keyword, token
  });

  const { budgetRisk, emiRisk, cashflowForecast, anomalies, loadingAdvanced } = useAdvancedAnalytics(token);

  return (
    <>
      <Navbar />
      <section className="section">
        <div className="container">
          <h1 className="title has-text-white">Financial Insights</h1>

          {/* Filters */}
          <FiltersPanel
            mode={mode} setMode={setMode}
            trendMode={trendMode} setTrendMode={setTrendMode}
            type={type} setType={setType}
            startDate={startDate} setStartDate={setStartDate}
            endDate={endDate} setEndDate={setEndDate}
            keyword={keyword} setKeyword={setKeyword}
          />

          {loadingBar && <progress className="progress is-small is-info" max="100">Loading…</progress>}
          {errorBar && <div className="notification is-danger">{errorBar}</div>}

          {/* Charts & Analytics */}
          <BarChartBlock data={barData} />
          <PieChartBlock data={categoryData} />
          <TrendChartBlock trendMode={trendMode} token={token} />

          <BudgetBreachCard data={budgetRisk} loading={loadingAdvanced} />
          <EMICard data={emiRisk} />
          <CashflowChartBlock data={cashflowForecast} />
          <AnomaliesTable data={anomalies} />

          <GoalCard />
          <SavingsCard />
        </div>
      </section>
      <Footer />
    </>
  );
}