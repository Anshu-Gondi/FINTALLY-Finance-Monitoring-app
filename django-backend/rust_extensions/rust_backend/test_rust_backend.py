import pytest
from datetime import datetime, timedelta
import random
import time
import rust_backend  # <-- compiled Rust module


# --- Helper to generate sample data ---
def generate_sample_data(n=1000):
    base = datetime(2024, 1, 1, 10, 0, 0)
    dates = [(base + timedelta(minutes=i)).isoformat() + "Z" for i in range(n)]
    prices = [random.uniform(-500, 1000) for _ in range(n)]
    categories = [random.choice(["food", "rent", "travel", "salary"]) for _ in range(n)]
    return dates, prices, categories


# --- Individual tests with performance timing ---
def test_aggregate_by_interval():
    dates, prices, _ = generate_sample_data(1000)
    start = time.perf_counter()
    res = rust_backend.aggregate_by_interval(dates, prices, 15)
    elapsed = time.perf_counter() - start
    assert isinstance(res, list)
    print(f"✅ aggregate_by_interval: {len(res)} results in {elapsed:.4f}s")
    assert elapsed < 1.0


def test_aggregate_by_category():
    _, prices, cats = generate_sample_data(500)
    start = time.perf_counter()
    res = rust_backend.aggregate_by_category(cats, prices, limit=3)
    elapsed = time.perf_counter() - start
    assert isinstance(res, list)
    print(f"✅ aggregate_by_category: {len(res)} results in {elapsed:.4f}s")
    assert elapsed < 1.0


def test_aggregate_by_day():
    dates, prices, _ = generate_sample_data(2000)
    start = time.perf_counter()
    res = rust_backend.aggregate_by_day(dates, prices, None)
    elapsed = time.perf_counter() - start
    assert all(len(r) == 4 for r in res)
    print(f"✅ aggregate_by_day: {len(res)} results in {elapsed:.4f}s")
    assert elapsed < 1.0


def test_aggregate_by_month():
    dates, prices, _ = generate_sample_data(3000)
    start = time.perf_counter()
    res = rust_backend.aggregate_by_month(dates, prices)
    elapsed = time.perf_counter() - start
    assert isinstance(res, list)
    print(f"✅ aggregate_by_month: {len(res)} results in {elapsed:.4f}s")
    assert elapsed < 1.0


def test_find_min_max():
    dates, prices, _ = generate_sample_data(1500)
    start = time.perf_counter()
    res = rust_backend.find_min_max(dates, prices)
    elapsed = time.perf_counter() - start
    assert isinstance(res, tuple)
    print(f"✅ find_min_max OK in {elapsed:.4f}s")
    assert elapsed < 1.0


def test_aggregate_trend_monthly():
    dates, prices, _ = generate_sample_data(2500)
    start = time.perf_counter()
    res = rust_backend.aggregate_trend(dates, prices, "monthly")
    elapsed = time.perf_counter() - start
    assert isinstance(res, list)
    print(f"✅ aggregate_trend (monthly): {len(res)} results in {elapsed:.4f}s")
    assert elapsed < 1.0


def test_aggregate_trend_weekly():
    dates, prices, _ = generate_sample_data(2500)
    start = time.perf_counter()
    res = rust_backend.aggregate_trend(dates, prices, "weekly")
    elapsed = time.perf_counter() - start
    assert isinstance(res, list)
    print(f"✅ aggregate_trend (weekly): {len(res)} results in {elapsed:.4f}s")
    assert elapsed < 1.0


# --- Run directly without pytest too ---
if __name__ == "__main__":
    print("🚀 Running Rust backend performance tests...\n")
    test_aggregate_by_interval()
    test_aggregate_by_category()
    test_aggregate_by_day()
    test_aggregate_by_month()
    test_find_min_max()
    test_aggregate_trend_monthly()
    test_aggregate_trend_weekly()
    print("\n🎉 All Rust-Python integration tests passed successfully.\n")
