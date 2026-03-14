import { useState, useEffect } from "react";

export default function useAdvancedAnalytics(token) {
  const [budgetRisk, setBudgetRisk] = useState(null);
  const [emiRisk, setEmiRisk] = useState(null);
  const [cashflowForecast, setCashflowForecast] = useState([]);
  const [anomalies, setAnomalies] = useState([]);
  const [loadingAdvanced, setLoadingAdvanced] = useState(false);

  useEffect(() => {
    if (!token) return;

    setLoadingAdvanced(true);

    const fetchAdvanced = async () => {
      try {
        const [resBudget, resEMI, resCashflow, resAnomalies] = await Promise.all([
          fetch(`${import.meta.env.VITE_API_URL}/insights/budget-breach?end_date=${new Date().toISOString().slice(0,10)}`, { headers: { Authorization: `Bearer ${token}` } }).then(r => r.json()),
          fetch(`${import.meta.env.VITE_API_URL}/insights/emi-pressure`, { headers: { Authorization: `Bearer ${token}` } }).then(r => r.json()),
          fetch(`${import.meta.env.VITE_API_URL}/insights/cashflow?horizons=30,60,90`, { headers: { Authorization: `Bearer ${token}` } }).then(r => r.json()),
          fetch(`${import.meta.env.VITE_API_URL}/insights/anomalies`, { headers: { Authorization: `Bearer ${token}` } }).then(r => r.json()),
        ]);

        setBudgetRisk(resBudget);
        setEmiRisk(resEMI);
        setCashflowForecast(resCashflow.data || []);
        setAnomalies(resAnomalies.data || []);
      } catch (err) {
        console.error(err);
      } finally {
        setLoadingAdvanced(false);
      }
    };

    fetchAdvanced();
  }, [token]);

  return { budgetRisk, emiRisk, cashflowForecast, anomalies, loadingAdvanced };
}