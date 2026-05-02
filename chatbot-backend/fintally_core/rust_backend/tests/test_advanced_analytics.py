import pytest
from datetime import datetime, timedelta
import random
import time
import rust_backend  # compiled Rust module


def test_emi_survivability_score():
    score, label = rust_backend.emi_survivability_score(
        monthly_income=100_000,
        monthly_emi=15_000
    )

    assert isinstance(score, int)
    assert 0 <= score <= 100
    assert label in {"Safe", "Manageable", "High Risk", "Critical"}


def test_emi_monthly_pressure_basic():
    dates = [
        "2024-01-10T00:00:00Z",
        "2024-01-15T00:00:00Z",
        "2024-02-10T00:00:00Z",
    ]
    principals = [500_000, 300_000, 200_000]
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
    prices = [50_000, -10_000]
    freqs = ["Monthly", "Weekly"]

    res = rust_backend.cashflow_forecast(
        start_dates, prices, freqs, horizon_days=60
    )

    assert isinstance(res, list)
    assert all(len(r) == 4 for r in res)

    dates = [r[0] for r in res]
    assert dates == sorted(dates)


def test_predict_budget_breach_mc():
    dates = [
        "2024-01-01T00:00:00Z",
        "2024-01-05T00:00:00Z",
        "2024-01-10T00:00:00Z",
    ]
    prices = [-5_000, -7_000, -6_000]

    prob, expected, p50_days = rust_backend.predict_budget_breach(
        dates,
        prices,
        budget_amount=15_000,
        start_date="2024-01-01T00:00:00Z",
        end_date="2024-02-01T00:00:00Z",
        simulations=2_000,
    )

    assert 0.0 <= prob <= 1.0
    assert expected > 0.0
    assert p50_days is None or p50_days >= 0

def test_detect_recurring_anomalies():
    dates = [
        "2024-01-01T00:00:00Z",
        "2024-01-31T00:00:00Z",
        "2024-03-10T00:00:00Z",  # missed recurrence
    ]
    prices = [-1_000, -1_000, -3_000]

    res = rust_backend.detect_recurring_anomalies(
        dates=dates,
        prices=prices,
        expected_frequency_days=30,
        tolerance_pct=0.3
    )

    assert isinstance(res, list)
    assert any(
        "Missed" in msg or "Amount" in msg
        for _, msg in res
    )


def test_detect_anomalies():
    prices = [100, 105, 98, 102, 5_000]

    res = rust_backend.detect_anomalies(prices, threshold=2.0)

    assert 5_000 in res


if __name__ == "__main__":
    print("🚀 Running Rust backend integration tests...\n")
    test_emi_survivability_score()
    test_emi_monthly_pressure_basic()
    test_cashflow_forecast()
    test_predict_budget_breach_mc()
    test_detect_recurring_anomalies()
    test_detect_anomalies()
    print("\n🎉 All tests passed successfully.\n")
