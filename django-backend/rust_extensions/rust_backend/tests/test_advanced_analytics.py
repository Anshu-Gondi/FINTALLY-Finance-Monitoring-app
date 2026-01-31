import pytest
from datetime import datetime, timedelta
import random
import time
import rust_backend  # <-- compiled Rust module

def test_emi_survivability_score():
    score, label = rust_backend.emi_survivability_score(
        monthly_income=100000,
        monthly_emi=15000
    )

    assert isinstance(score, int)
    assert 0 <= score <= 100
    assert label in {"Safe", "Manageable", "High Risk", "Critical"}

def test_emi_monthly_pressure():
    dates = [
        "2024-01-10T00:00:00Z",
        "2024-01-15T00:00:00Z",
        "2024-02-10T00:00:00Z",
    ]
    principals = [500000, 300000, 200000]
    rates = [10.0, 12.0, 8.0]
    tenures = [60, 48, 24]

    res = rust_backend.emi_monthly_pressure(
        dates, principals, rates, tenures
    )

    assert isinstance(res, list)
    assert all(len(r) == 2 for r in res)
    assert res[0][0].startswith("2024-")

def test_emi_monthly_pressure():
    dates = [
        "2024-01-10T00:00:00Z",
        "2024-01-15T00:00:00Z",
        "2024-02-10T00:00:00Z",
    ]
    principals = [500000, 300000, 200000]
    rates = [10.0, 12.0, 8.0]
    tenures = [60, 48, 24]

    res = rust_backend.emi_monthly_pressure(
        dates, principals, rates, tenures
    )

    assert isinstance(res, list)
    assert all(len(r) == 2 for r in res)
    assert res[0][0].startswith("2024-")

def test_cashflow_forecast():
    start_dates = [
        "2024-01-01T00:00:00Z",
        "2024-01-05T00:00:00Z",
    ]
    prices = [50000, -10000]
    freqs = ["Monthly", "Weekly"]

    res = rust_backend.cashflow_forecast(
        start_dates, prices, freqs, horizon_days=60
    )

    assert isinstance(res, list)
    assert all(len(r) == 4 for r in res)

    # ensure sorted by date
    dates = [r[0] for r in res]
    assert dates == sorted(dates)

def test_predict_budget_breach():
    dates = [
        "2024-01-01T00:00:00Z",
        "2024-01-05T00:00:00Z",
        "2024-01-10T00:00:00Z",
    ]
    prices = [-5000, -7000, -6000]

    will_breach, projected, days_left = rust_backend.predict_budget_breach(
        dates=dates,
        prices=prices,
        budget_amount=15000,
        start_date="2024-01-01T00:00:00Z",
        end_date="2024-02-01T00:00:00Z"
    )

    assert isinstance(will_breach, bool)
    assert isinstance(projected, float)

def test_detect_recurring_anomalies():
    dates = [
        "2024-01-01T00:00:00Z",
        "2024-01-31T00:00:00Z",  # OK
        "2024-03-10T00:00:00Z",  # Missed
    ]
    prices = [-1000, -1000, -3000]

    res = rust_backend.detect_recurring_anomalies(
        dates=dates,
        prices=prices,
        expected_frequency_days=30,
        tolerance_pct=0.3
    )

    assert isinstance(res, list)
    assert any("Missed" in r[1] or "Amount" in r[1] for r in res)

def test_detect_anomalies():
    prices = [100, 105, 98, 102, 5000]  # clear outlier

    res = rust_backend.detect_anomalies(prices, threshold=2.0)

    assert 5000 in res

# --- Run directly without pytest too ---
if __name__ == "__main__":
    print("🚀 Running Rust backend performance tests...\n")
    test_emi_survivability_score()
    test_emi_monthly_pressure()
    test_cashflow_forecast()
    test_predict_budget_breach()
    test_detect_recurring_anomalies()
    test_detect_anomalies()
    print("\n🎉 All Rust-Python integration tests passed successfully.\n")

