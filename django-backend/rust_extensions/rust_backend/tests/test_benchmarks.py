import pytest
from datetime import datetime, timedelta
import random
import time
import rust_backend  # <-- compiled Rust module

# ----------------------------
# Helper to generate sample data
# ----------------------------
def generate_sample_data(n=1000):
    base = datetime(2024, 1, 1, 10, 0, 0)
    dates = [(base + timedelta(minutes=i)).isoformat() + "Z" for i in range(n)]
    prices = [random.uniform(-500, 1000) for _ in range(n)]
    categories = [random.choice(["food", "rent", "travel", "salary"]) for _ in range(n)]
    return dates, prices, categories

# ----------------------------
# Correctness tests (small datasets)
# ----------------------------
def test_emi_survivability_score():
    score, label = rust_backend.emi_survivability_score(monthly_income=100_000, monthly_emi=15_000)
    assert isinstance(score, int)
    assert 0 <= score <= 100
    assert label in {"Safe", "Manageable", "High Risk", "Critical"}

def test_emi_monthly_pressure():
    dates = ["2024-01-10T00:00:00Z", "2024-01-15T00:00:00Z", "2024-02-10T00:00:00Z"]
    principals = [500_000, 300_000, 200_000]
    rates = [10.0, 12.0, 8.0]
    tenures = [60, 48, 24]

    res = rust_backend.emi_monthly_pressure(dates, principals, rates, tenures)
    assert isinstance(res, list)
    assert all(len(r) == 2 for r in res)
    assert res[0][0].startswith("2024-")

def test_cashflow_forecast():
    start_dates = ["2024-01-01T00:00:00Z", "2024-01-05T00:00:00Z"]
    prices = [50000, -10000]
    freqs = ["Monthly", "Weekly"]

    res = rust_backend.cashflow_forecast(start_dates, prices, freqs, horizon_days=60)
    assert isinstance(res, list)
    assert all(len(r) == 4 for r in res)
    dates = [r[0] for r in res]
    assert dates == sorted(dates)

def test_predict_budget_breach():
    dates = ["2024-01-01T00:00:00Z", "2024-01-05T00:00:00Z", "2024-01-10T00:00:00Z"]
    prices = [-5000, -7000, -6000]

    will_breach, projected, days_left = rust_backend.predict_budget_breach(
        dates, prices, budget_amount=15_000,
        start_date="2024-01-01T00:00:00Z",
        end_date="2024-02-01T00:00:00Z"
    )

    assert isinstance(will_breach, bool)
    assert isinstance(projected, float)

def test_detect_recurring_anomalies():
    dates = ["2024-01-01T00:00:00Z", "2024-01-31T00:00:00Z", "2024-03-10T00:00:00Z"]
    prices = [-1000, -1000, -3000]

    res = rust_backend.detect_recurring_anomalies(dates, prices, expected_frequency_days=30, tolerance_pct=0.3)
    assert isinstance(res, list)
    assert any("Missed" in r[1] or "Amount" in r[1] for r in res)

def test_detect_anomalies():
    prices = [100, 105, 98, 102, 5000]
    res = rust_backend.detect_anomalies(prices, threshold=2.0)
    assert 5000 in res

# ----------------------------
# Performance / benchmark tests (larger datasets)
# ----------------------------
def benchmark(func, *args, label=None, max_time=1.0):
    start = time.perf_counter()
    res = func(*args)
    elapsed = time.perf_counter() - start
    if label:
        print(f"✅ {label}: {len(res) if isinstance(res, list) else res} in {elapsed:.4f}s")
    assert elapsed < max_time, f"{label} exceeded {max_time}s"
    return res

def test_aggregate_by_interval_perf():
    dates, prices, _ = generate_sample_data(1_000_000)  # 1M rows
    benchmark(rust_backend.aggregate_by_interval, dates, prices, 15, label="aggregate_by_interval", max_time=10.0)

def test_aggregate_by_category_perf():
    _, prices, cats = generate_sample_data(1_000_000)
    benchmark(rust_backend.aggregate_by_category, cats, prices, 10, label="aggregate_by_category", max_time=5.0)

def test_aggregate_by_day_perf():
    dates, prices, _ = generate_sample_data(1_000_000)
    benchmark(rust_backend.aggregate_by_day, dates, prices, None, label="aggregate_by_day", max_time=10.0)

def test_aggregate_by_month_perf():
    dates, prices, _ = generate_sample_data(1_000_000)
    benchmark(rust_backend.aggregate_by_month, dates, prices, label="aggregate_by_month", max_time=10.0)

def test_find_min_max_perf():
    dates, prices, _ = generate_sample_data(1_000_000)
    benchmark(rust_backend.find_min_max, dates, prices, label="find_min_max", max_time=5.0)

def test_aggregate_trend_monthly_perf():
    dates, prices, _ = generate_sample_data(1_000_000)
    benchmark(rust_backend.aggregate_trend, dates, prices, "monthly", label="aggregate_trend_monthly", max_time=10.0)

def test_aggregate_trend_weekly_perf():
    dates, prices, _ = generate_sample_data(1_000_000)
    benchmark(rust_backend.aggregate_trend, dates, prices, "weekly", label="aggregate_trend_weekly", max_time=10.0)

# ----------------------------
# Run directly without pytest
# ----------------------------
if __name__ == "__main__":
    print("🚀 Running Rust backend integration + 1M-row performance tests...\n")
    test_emi_survivability_score()
    test_emi_monthly_pressure()
    test_cashflow_forecast()
    test_predict_budget_breach()
    test_detect_recurring_anomalies()
    test_detect_anomalies()
    test_aggregate_by_interval_perf()
    test_aggregate_by_category_perf()
    test_aggregate_by_day_perf()
    test_aggregate_by_month_perf()
    test_find_min_max_perf()
    test_aggregate_trend_monthly_perf()
    test_aggregate_trend_weekly_perf()
    print("\n🎉 All Rust-Python tests + 1M-row benchmarks passed successfully!\n")
