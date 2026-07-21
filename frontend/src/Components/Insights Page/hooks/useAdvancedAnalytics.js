/**
 * useAdvancedAnalytics.js
 * Fetches budget breach, EMI pressure, cashflow forecast, and anomalies
 * using analyticsApi — no raw fetch calls.
 */
import { useState, useEffect } from "react";
import { analyticsApi } from "../../../services/api";

export default function useAdvancedAnalytics() {
  const [budgetRisk, setBudgetRisk] = useState(null);
  const [emiRisk, setEmiRisk] = useState(null);
  const [cashflowForecast, setCashflowForecast] = useState(null);
  const [anomalies, setAnomalies] = useState([]);
  const [loadingAdvanced, setLoadingAdvanced] = useState(false);

  useEffect(() => {
    let cancelled = false;
    setLoadingAdvanced(true);

    // end_date required by budget-breach — default to 30 days out
    const endDate = new Date(Date.now() + 30 * 24 * 60 * 60 * 1000)
      .toISOString()
      .split("T")[0];

    // Note: You can pass a lower threshold here (e.g. 1.2 or 1.5) to test sensitivity
    Promise.allSettled([
      analyticsApi.budgetBreach(endDate),
      analyticsApi.emiPressure(),
      analyticsApi.cashflowForecast([30, 60, 90]),
      analyticsApi.anomalies(1.5), // Lowered threshold slightly for testing
    ]).then(([breach, emi, cashflow, anom]) => {
      if (cancelled) return;

      if (breach.status === "fulfilled") setBudgetRisk(breach.value);
      if (emi.status === "fulfilled")    setEmiRisk(emi.value);
      if (cashflow.status === "fulfilled") setCashflowForecast(cashflow.value);

      // 🔍 DEBUG LOGS FOR ANOMALIES
      if (anom.status === "fulfilled") {
        setAnomalies(anom.value?.anomalies ?? []);
      } else {
        console.error("❌ Anomalies API Request Rejected/Failed:", anom.reason);
      }

      setLoadingAdvanced(false);
    });

    return () => { cancelled = true; };
  }, []);

  return { budgetRisk, emiRisk, cashflowForecast, anomalies, loadingAdvanced };
}
