"""
tests for fintally_core/rust_backend

Structure:
  test_base_analytics.py   — aggregate_*, find_min_max, aggregate_trend (perf + shape, already exists)
  test_advanced_analytics.py — emi/budget/cashflow/anomaly (already exists, has a bug)

This file: complete correctness + value contract tests for ALL 19 functions.
Replaces + extends both existing files.

What the existing tests already do (kept, not removed):
  - Shape checks (isinstance, len)
  - Basic perf benchmarks for aggregate_* at 1000–3000 rows

What this file adds:
  - Value contract tests for every function
  - Edge cases: empty inputs, zero values, single element
  - Sort-order guarantees
  - Math correctness: budget_utilization formula, burn_rate formula,
    savings_metrics thresholds, net_worth_analysis, recurring_impact factors
  - All 7 untested functions: budget_utilization, budget_burn_rate,
    recurring_impact, category_drift, income_stability, savings_metrics,
    net_worth_analysis
  - predict_budget_breach signature fix (no start_date/end_date params in Rust)
  - aggregate_by_category limit param correctness
  - aggregate_by_day bucket_days grouping
  - find_min_max correctness on known data
  - emi_survivability_score all 4 tiers
  - detect_anomalies MAD correctness
  - cashflow_forecast date ordering and net calculation

Run:
    pytest fintally_core/rust_backend/tests/ -v
"""

import pytest
import time
import random
from datetime import datetime, timedelta, timezone

try:
    import rust_backend
except ImportError:
    pytest.skip(
        "rust_backend not built. Run `maturin develop` in fintally_core/rust_backend/.",
        allow_module_level=True,
    )


# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

def _dates(n: int, start="2024-01-01T00:00:00Z", step_minutes=1) -> list[str]:
    base = datetime(2024, 1, 1, 0, 0, 0, tzinfo=timezone.utc)
    return [
        (base + timedelta(minutes=i * step_minutes)).strftime("%Y-%m-%dT%H:%M:%SZ")
        for i in range(n)
    ]


def _daily_dates(n: int, start="2024-01-01") -> list[str]:
    base = datetime(2024, 1, 1, tzinfo=timezone.utc)
    return [
        (base + timedelta(days=i)).strftime("%Y-%m-%dT%H:%M:%SZ")
        for i in range(n)
    ]


def _rand_prices(n: int, seed=42) -> list[float]:
    random.seed(seed)
    return [random.uniform(-500, 1000) for _ in range(n)]


def _rand_categories(n: int, seed=42) -> list[str]:
    random.seed(seed)
    return [random.choice(["food", "rent", "travel", "salary"]) for _ in range(n)]


# ═══════════════════════════════════════════════════════════════════════════════
# 1. aggregate_by_interval
# ═══════════════════════════════════════════════════════════════════════════════

class TestAggregateByInterval:

    def test_returns_list_of_4_tuples(self):
        dates = _dates(100)
        prices = _rand_prices(100)
        res = rust_backend.aggregate_by_interval(dates, prices, 15)
        assert isinstance(res, list)
        assert all(len(r) == 4 for r in res)

    def test_label_format_HH_MM(self):
        dates = _dates(60, step_minutes=1)
        prices = [100.0] * 60
        res = rust_backend.aggregate_by_interval(dates, prices, 15)
        for label, *_ in res:
            parts = label.split(":")
            assert len(parts) == 2, f"Bad label: {label}"
            assert parts[0].isdigit() and parts[1].isdigit()

    def test_income_and_expense_separated(self):
        """Positive prices go to income column, negative to expense column."""
        dates = _dates(4, step_minutes=20)
        prices = [1000.0, -200.0, 500.0, -100.0]
        res = rust_backend.aggregate_by_interval(dates, prices, 60)
        for label, income, expense, total in res:
            assert income >= 0.0
            assert expense >= 0.0
            assert total == pytest.approx(income + expense, abs=0.01)

    def test_total_equals_income_plus_expense_abs(self):
        dates = _dates(200, step_minutes=1)
        prices = _rand_prices(200)
        res = rust_backend.aggregate_by_interval(dates, prices, 15)
        for label, income, expense, total in res:
            assert total == pytest.approx(income + expense, abs=0.01)

    def test_empty_input_returns_empty(self):
        res = rust_backend.aggregate_by_interval([], [], 15)
        assert res == []

    def test_single_row(self):
        res = rust_backend.aggregate_by_interval(
            ["2024-01-01T10:30:00Z"], [500.0], 15
        )
        assert len(res) == 1
        assert res[0][1] == pytest.approx(500.0)  # income

    def test_performance_1000_rows(self):
        dates = _dates(1000, step_minutes=1)
        prices = _rand_prices(1000)
        t0 = time.perf_counter()
        rust_backend.aggregate_by_interval(dates, prices, 15)
        assert time.perf_counter() - t0 < 1.0


# ═══════════════════════════════════════════════════════════════════════════════
# 2. aggregate_by_category
# ═══════════════════════════════════════════════════════════════════════════════

class TestAggregateByCategory:

    def test_returns_list_of_3_tuples(self):
        cats = _rand_categories(100)
        prices = _rand_prices(100)
        res = rust_backend.aggregate_by_category(cats, prices, None)
        assert isinstance(res, list)
        assert all(len(r) == 3 for r in res)

    def test_sorted_descending_by_total(self):
        cats = _rand_categories(500)
        prices = _rand_prices(500)
        res = rust_backend.aggregate_by_category(cats, prices, None)
        totals = [r[1] for r in res]
        assert totals == sorted(totals, reverse=True)

    def test_limit_respected(self):
        cats = _rand_categories(500)
        prices = _rand_prices(500)
        res = rust_backend.aggregate_by_category(cats, prices, 2)
        assert len(res) <= 2

    def test_limit_none_returns_all_categories(self):
        cats = ["food", "rent", "travel", "salary"] * 25
        prices = [100.0] * 100
        res = rust_backend.aggregate_by_category(cats, prices, None)
        returned_cats = {r[0] for r in res}
        assert returned_cats == {"food", "rent", "travel", "salary"}

    def test_counts_are_correct(self):
        cats = ["food", "food", "rent", "rent", "rent"]
        prices = [100.0, 200.0, 150.0, 150.0, 150.0]
        res = rust_backend.aggregate_by_category(cats, prices, None)
        by_name = {r[0]: (r[1], r[2]) for r in res}
        assert by_name["food"][1] == 2
        assert by_name["rent"][1] == 3

    def test_totals_use_absolute_values(self):
        cats = ["expense", "expense"]
        prices = [-100.0, -200.0]
        res = rust_backend.aggregate_by_category(cats, prices, None)
        assert res[0][1] == pytest.approx(300.0)

    def test_empty_category_skipped(self):
        cats = ["food", "", "rent"]
        prices = [100.0, 999.0, 200.0]
        res = rust_backend.aggregate_by_category(cats, prices, None)
        cat_names = [r[0] for r in res]
        assert "" not in cat_names

    def test_performance_500_rows(self):
        cats = _rand_categories(500)
        prices = _rand_prices(500)
        t0 = time.perf_counter()
        rust_backend.aggregate_by_category(cats, prices, 10)
        assert time.perf_counter() - t0 < 1.0


# ═══════════════════════════════════════════════════════════════════════════════
# 3. aggregate_by_day
# ═══════════════════════════════════════════════════════════════════════════════

class TestAggregateByDay:

    def test_returns_list_of_4_tuples(self):
        dates = _daily_dates(30)
        prices = _rand_prices(30)
        res = rust_backend.aggregate_by_day(dates, prices, None)
        assert all(len(r) == 4 for r in res)

    def test_sorted_by_date(self):
        dates = _daily_dates(30)
        prices = _rand_prices(30)
        res = rust_backend.aggregate_by_day(dates, prices, None)
        date_labels = [r[0] for r in res]
        assert date_labels == sorted(date_labels)

    def test_date_format_YYYY_MM_DD(self):
        dates = _daily_dates(5)
        prices = [100.0] * 5
        res = rust_backend.aggregate_by_day(dates, prices, None)
        for label, *_ in res:
            parts = label.split("-")
            assert len(parts) == 3

    def test_bucket_days_groups_correctly(self):
        dates = _daily_dates(6)
        prices = [100.0] * 6
        res = rust_backend.aggregate_by_day(dates, prices, 2)
        assert len(res) == 3  # 6 days / 2 = 3 groups
        for label, *_ in res:
            assert label.startswith("Group")

    def test_income_expense_separated(self):
        dates = _daily_dates(4)
        prices = [500.0, -200.0, 300.0, -100.0]
        res = rust_backend.aggregate_by_day(dates, prices, None)
        for _, income, expense, total in res:
            assert income >= 0.0
            assert expense >= 0.0

    def test_total_equals_income_plus_expense(self):
        dates = _daily_dates(10)
        prices = _rand_prices(10)
        res = rust_backend.aggregate_by_day(dates, prices, None)
        for _, income, expense, total in res:
            assert total == pytest.approx(income + expense, abs=0.01)

    def test_performance_2000_rows(self):
        dates = _daily_dates(2000)
        prices = _rand_prices(2000)
        t0 = time.perf_counter()
        rust_backend.aggregate_by_day(dates, prices, None)
        assert time.perf_counter() - t0 < 1.0


# ═══════════════════════════════════════════════════════════════════════════════
# 4. aggregate_by_month
# ═══════════════════════════════════════════════════════════════════════════════

class TestAggregateByMonth:

    def test_returns_list_of_4_tuples(self):
        dates = _daily_dates(90)
        prices = _rand_prices(90)
        res = rust_backend.aggregate_by_month(dates, prices)
        assert all(len(r) == 4 for r in res)

    def test_sorted_chronologically(self):
        dates = _daily_dates(90)
        prices = _rand_prices(90)
        res = rust_backend.aggregate_by_month(dates, prices)
        keys = [r[0] for r in res]
        assert keys == sorted(keys)

    def test_label_format_YYYY_MM(self):
        dates = _daily_dates(30)
        prices = [100.0] * 30
        res = rust_backend.aggregate_by_month(dates, prices)
        for label, *_ in res:
            parts = label.split("-")
            assert len(parts) == 2
            assert len(parts[0]) == 4  # year
            assert len(parts[1]) == 2  # month

    def test_income_and_expense_correct(self):
        dates = ["2024-01-01T00:00:00Z", "2024-01-02T00:00:00Z"]
        prices = [1000.0, -400.0]
        res = rust_backend.aggregate_by_month(dates, prices)
        assert len(res) == 1
        label, income, expense, total = res[0]
        assert label == "2024-01"
        assert income == pytest.approx(1000.0)
        assert expense == pytest.approx(400.0)
        assert total == pytest.approx(1400.0)

    def test_performance_3000_rows(self):
        dates = _daily_dates(3000)
        prices = _rand_prices(3000)
        t0 = time.perf_counter()
        rust_backend.aggregate_by_month(dates, prices)
        assert time.perf_counter() - t0 < 1.0


# ═══════════════════════════════════════════════════════════════════════════════
# 5. find_min_max
# ═══════════════════════════════════════════════════════════════════════════════

class TestFindMinMax:

    def test_returns_tuple_of_two(self):
        dates, prices = _daily_dates(10), _rand_prices(10)
        res = rust_backend.find_min_max(dates, prices)
        assert isinstance(res, tuple)
        assert len(res) == 2

    def test_known_min_and_max(self):
        dates = ["2024-01-01T00:00:00Z", "2024-01-02T00:00:00Z", "2024-01-03T00:00:00Z"]
        prices = [100.0, 5000.0, -300.0]
        min_res, max_res = rust_backend.find_min_max(dates, prices)
        # min_res is the smallest value
        assert min_res is not None
        assert min_res[1] == pytest.approx(-300.0)
        assert max_res is not None
        assert max_res[1] == pytest.approx(5000.0)

    def test_single_element(self):
        min_res, max_res = rust_backend.find_min_max(
            ["2024-01-01T00:00:00Z"], [42.0]
        )
        assert min_res is not None
        assert max_res is not None
        assert min_res[1] == pytest.approx(42.0)
        assert max_res[1] == pytest.approx(42.0)

    def test_empty_returns_none_none(self):
        min_res, max_res = rust_backend.find_min_max([], [])
        assert min_res is None
        assert max_res is None

    def test_date_is_str(self):
        dates = _daily_dates(5)
        prices = _rand_prices(5)
        min_res, max_res = rust_backend.find_min_max(dates, prices)
        assert isinstance(min_res[0], str)
        assert isinstance(max_res[0], str)

    def test_performance_1500_rows(self):
        dates = _daily_dates(1500)
        prices = _rand_prices(1500)
        t0 = time.perf_counter()
        rust_backend.find_min_max(dates, prices)
        assert time.perf_counter() - t0 < 1.0


# ═══════════════════════════════════════════════════════════════════════════════
# 6. aggregate_trend
# ═══════════════════════════════════════════════════════════════════════════════

class TestAggregateTrend:

    def test_monthly_returns_YYYY_MM_keys(self):
        dates = _daily_dates(60)
        prices = _rand_prices(60)
        res = rust_backend.aggregate_trend(dates, prices, "monthly")
        for label, *_ in res:
            assert "-" in label
            assert len(label.split("-")) == 2

    def test_weekly_returns_ISO_week_keys(self):
        dates = _daily_dates(60)
        prices = _rand_prices(60)
        res = rust_backend.aggregate_trend(dates, prices, "weekly")
        for label, *_ in res:
            assert "-W" in label

    def test_sorted_chronologically(self):
        dates = _daily_dates(90)
        prices = _rand_prices(90)
        for mode in ("monthly", "weekly"):
            res = rust_backend.aggregate_trend(dates, prices, mode)
            keys = [r[0] for r in res]
            assert keys == sorted(keys), f"Not sorted for mode={mode}"

    def test_unknown_mode_defaults_to_monthly(self):
        """Rust matches 'monthly' as default for unknown mode strings."""
        dates = _daily_dates(30)
        prices = _rand_prices(30)
        res_default = rust_backend.aggregate_trend(dates, prices, "monthly")
        res_unknown = rust_backend.aggregate_trend(dates, prices, "quarterly")
        assert len(res_default) == len(res_unknown)

    def test_total_equals_income_plus_expense(self):
        dates = _daily_dates(30)
        prices = _rand_prices(30)
        res = rust_backend.aggregate_trend(dates, prices, "monthly")
        for _, income, expense, total in res:
            assert total == pytest.approx(income + expense, abs=0.01)

    def test_performance_2500_rows(self):
        dates = _daily_dates(2500)
        prices = _rand_prices(2500)
        for mode in ("monthly", "weekly"):
            t0 = time.perf_counter()
            rust_backend.aggregate_trend(dates, prices, mode)
            assert time.perf_counter() - t0 < 1.0


# ═══════════════════════════════════════════════════════════════════════════════
# 7. budget_utilization  (previously untested)
# ═══════════════════════════════════════════════════════════════════════════════

class TestBudgetUtilization:

    def test_returns_3_tuple(self):
        res = rust_backend.budget_utilization(10_000.0, [500.0, 300.0, 200.0])
        assert isinstance(res, tuple)
        assert len(res) == 3

    def test_spent_is_sum_of_abs_prices(self):
        prices = [-300.0, -200.0, -500.0]
        spent, remaining, pct = rust_backend.budget_utilization(2000.0, prices)
        assert spent == pytest.approx(1000.0)

    def test_remaining_is_budget_minus_spent(self):
        prices = [-300.0, -200.0]
        spent, remaining, pct = rust_backend.budget_utilization(1000.0, prices)
        assert remaining == pytest.approx(500.0)

    def test_remaining_floors_at_zero(self):
        """Over-budget: remaining must be 0, not negative."""
        prices = [-2000.0]
        spent, remaining, pct = rust_backend.budget_utilization(1000.0, prices)
        assert remaining == pytest.approx(0.0)

    def test_usage_percent_formula(self):
        prices = [-500.0]
        spent, remaining, pct = rust_backend.budget_utilization(1000.0, prices)
        assert pct == pytest.approx(50.0)

    def test_zero_budget_returns_zero_percent(self):
        spent, remaining, pct = rust_backend.budget_utilization(0.0, [-100.0])
        assert pct == pytest.approx(0.0)

    def test_full_utilization(self):
        prices = [-1000.0]
        spent, remaining, pct = rust_backend.budget_utilization(1000.0, prices)
        assert pct == pytest.approx(100.0)
        assert remaining == pytest.approx(0.0)

    def test_empty_prices_means_zero_spent(self):
        spent, remaining, pct = rust_backend.budget_utilization(5000.0, [])
        assert spent == pytest.approx(0.0)
        assert remaining == pytest.approx(5000.0)
        assert pct == pytest.approx(0.0)


# ═══════════════════════════════════════════════════════════════════════════════
# 8. budget_burn_rate  (previously untested)
# ═══════════════════════════════════════════════════════════════════════════════

class TestBudgetBurnRate:

    def test_returns_2_tuple(self):
        res = rust_backend.budget_burn_rate(
            total_spent=9000.0, days_elapsed=30, budget_remaining=6000.0
        )
        assert isinstance(res, tuple)
        assert len(res) == 2

    def test_zero_days_returns_zero_burn_no_days_left(self):
        burn, days_left = rust_backend.budget_burn_rate(
            total_spent=1000.0, days_elapsed=0, budget_remaining=5000.0
        )
        assert burn == pytest.approx(0.0)
        assert days_left is None

    def test_burn_rate_formula(self):
        burn, _ = rust_backend.budget_burn_rate(
            total_spent=9000.0, days_elapsed=30, budget_remaining=6000.0
        )
        assert burn == pytest.approx(300.0)  # 9000 / 30

    def test_days_left_formula(self):
        _, days_left = rust_backend.budget_burn_rate(
            total_spent=9000.0, days_elapsed=30, budget_remaining=6000.0
        )
        # 6000 / 300 = 20 days
        assert days_left is not None
        assert days_left == pytest.approx(20.0)

    def test_zero_burn_returns_none_days_left(self):
        """If burn_rate is 0 (no spending), days_left is None (infinite)."""
        burn, days_left = rust_backend.budget_burn_rate(
            total_spent=0.0, days_elapsed=30, budget_remaining=5000.0
        )
        assert burn == pytest.approx(0.0)
        assert days_left is None

    def test_days_left_is_float_or_none(self):
        _, days_left = rust_backend.budget_burn_rate(
            total_spent=3000.0, days_elapsed=10, budget_remaining=9000.0
        )
        assert days_left is None or isinstance(days_left, float)


# ═══════════════════════════════════════════════════════════════════════════════
# 9. recurring_impact  (previously untested)
# ═══════════════════════════════════════════════════════════════════════════════

class TestRecurringImpact:

    def test_returns_2_tuple(self):
        res = rust_backend.recurring_impact([1000.0], ["Monthly"])
        assert isinstance(res, tuple)
        assert len(res) == 2

    def test_monthly_factor_is_1(self):
        monthly, yearly = rust_backend.recurring_impact([1000.0], ["Monthly"])
        assert monthly == pytest.approx(1000.0)

    def test_weekly_factor_is_4(self):
        monthly, _ = rust_backend.recurring_impact([1000.0], ["Weekly"])
        assert monthly == pytest.approx(4000.0)

    def test_daily_factor_is_30(self):
        monthly, _ = rust_backend.recurring_impact([100.0], ["Daily"])
        assert monthly == pytest.approx(3000.0)

    def test_yearly_is_monthly_times_12(self):
        monthly, yearly = rust_backend.recurring_impact(
            [1000.0, 500.0], ["Monthly", "Weekly"]
        )
        assert yearly == pytest.approx(monthly * 12.0)

    def test_uses_absolute_prices(self):
        """Negative prices (expenses) must still produce positive monthly total."""
        monthly, _ = rust_backend.recurring_impact([-500.0], ["Monthly"])
        assert monthly == pytest.approx(500.0)

    def test_unknown_frequency_defaults_to_monthly_factor_1(self):
        monthly, _ = rust_backend.recurring_impact([1000.0], ["Quarterly"])
        assert monthly == pytest.approx(1000.0)

    def test_mixed_frequencies(self):
        monthly, yearly = rust_backend.recurring_impact(
            [1000.0, 250.0, 50.0],
            ["Monthly", "Weekly", "Daily"]
        )
        # 1000×1 + 250×4 + 50×30 = 1000 + 1000 + 1500 = 3500
        assert monthly == pytest.approx(3500.0)
        assert yearly == pytest.approx(42_000.0)

    def test_empty_returns_zero(self):
        monthly, yearly = rust_backend.recurring_impact([], [])
        assert monthly == pytest.approx(0.0)
        assert yearly == pytest.approx(0.0)


# ═══════════════════════════════════════════════════════════════════════════════
# 10. category_drift  (previously untested)
# ═══════════════════════════════════════════════════════════════════════════════

class TestCategoryDrift:

    def test_returns_list_of_2_tuples(self):
        prev = {"food": 1000.0, "rent": 5000.0}
        curr = {"food": 1200.0, "rent": 5000.0}
        res = rust_backend.category_drift(prev, curr)
        assert isinstance(res, list)
        assert all(len(r) == 2 for r in res)

    def test_drift_formula(self):
        """20% increase in food: (1200-1000)/1000 * 100 = 20.0"""
        prev = {"food": 1000.0}
        curr = {"food": 1200.0}
        res = rust_backend.category_drift(prev, curr)
        assert len(res) == 1
        assert res[0][0] == "food"
        assert res[0][1] == pytest.approx(20.0)

    def test_sorted_descending_by_drift(self):
        prev = {"food": 1000.0, "rent": 1000.0, "travel": 1000.0}
        curr = {"food": 1500.0, "rent": 1100.0, "travel": 1200.0}
        res = rust_backend.category_drift(prev, curr)
        drifts = [r[1] for r in res]
        assert drifts == sorted(drifts, reverse=True)

    def test_new_category_in_current_not_in_previous_ignored(self):
        """Categories only in current (no previous baseline) should not appear."""
        prev = {"food": 1000.0}
        curr = {"food": 1200.0, "new_category": 999.0}
        res = rust_backend.category_drift(prev, curr)
        cats = [r[0] for r in res]
        assert "new_category" not in cats

    def test_zero_previous_value_skipped(self):
        """Zero previous value: division by zero case must be handled."""
        prev = {"food": 0.0}
        curr = {"food": 500.0}
        res = rust_backend.category_drift(prev, curr)
        # Rust skips prev_val <= 0, so result should be empty
        assert res == []

    def test_negative_drift_decrease(self):
        prev = {"food": 1000.0}
        curr = {"food": 800.0}
        res = rust_backend.category_drift(prev, curr)
        assert res[0][1] == pytest.approx(-20.0)

    def test_empty_returns_empty(self):
        assert rust_backend.category_drift({}, {}) == []


# ═══════════════════════════════════════════════════════════════════════════════
# 11. detect_anomalies
# ═══════════════════════════════════════════════════════════════════════════════

class TestDetectAnomalies:

    def test_detects_obvious_outlier(self):
        prices = [100.0, 105.0, 98.0, 102.0, 5000.0]
        res = rust_backend.detect_anomalies(prices, threshold=2.0)
        assert 5000.0 in res

    def test_no_anomalies_in_uniform_data(self):
        prices = [100.0, 100.0, 100.0, 100.0]
        res = rust_backend.detect_anomalies(prices, threshold=2.0)
        assert res == []

    def test_single_element_returns_empty(self):
        res = rust_backend.detect_anomalies([1000.0], threshold=2.0)
        assert res == []

    def test_empty_returns_empty(self):
        res = rust_backend.detect_anomalies([], threshold=2.0)
        assert res == []

    def test_returns_list_of_floats(self):
        prices = [100.0, 105.0, 98.0, 5000.0]
        res = rust_backend.detect_anomalies(prices, threshold=2.0)
        assert isinstance(res, list)
        assert all(isinstance(v, float) for v in res)

    def test_strict_threshold_catches_more(self):
        prices = [100.0, 130.0, 98.0, 102.0, 5000.0]
        loose = rust_backend.detect_anomalies(prices, threshold=3.0)
        strict = rust_backend.detect_anomalies(prices, threshold=1.0)
        assert len(strict) >= len(loose)


# ═══════════════════════════════════════════════════════════════════════════════
# 12. emi_survivability_score
# ═══════════════════════════════════════════════════════════════════════════════

class TestEmiSurvivabilityScore:

    def test_returns_int_and_str(self):
        score, label = rust_backend.emi_survivability_score(100_000, 15_000)
        assert isinstance(score, int)
        assert isinstance(label, str)

    def test_score_in_range(self):
        score, _ = rust_backend.emi_survivability_score(100_000, 15_000)
        assert 0 <= score <= 100

    def test_all_four_tiers(self):
        """Test that each EMI ratio band returns the correct label."""
        # ratio ≤ 0.20 → Safe
        score, label = rust_backend.emi_survivability_score(100_000, 15_000)  # 15%
        assert label == "Safe"
        assert score == 95

        # 0.20 < ratio ≤ 0.35 → Manageable
        score, label = rust_backend.emi_survivability_score(100_000, 30_000)  # 30%
        assert label == "Manageable"
        assert score == 80

        # 0.35 < ratio ≤ 0.50 → High Risk
        score, label = rust_backend.emi_survivability_score(100_000, 45_000)  # 45%
        assert label == "High Risk"
        assert score == 55

        # ratio > 0.50 → Critical
        score, label = rust_backend.emi_survivability_score(100_000, 60_000)  # 60%
        assert label == "Critical"
        assert score == 25

    def test_zero_income_returns_zero_score(self):
        score, label = rust_backend.emi_survivability_score(0.0, 10_000)
        assert score == 0


# ═══════════════════════════════════════════════════════════════════════════════
# 13. emi_monthly_pressure
# ═══════════════════════════════════════════════════════════════════════════════

class TestEmiMonthlyPressure:

    def test_returns_list_of_2_tuples(self):
        dates = ["2024-01-10T00:00:00Z"]
        res = rust_backend.emi_monthly_pressure(dates, [500_000], [10.0], [60])
        assert isinstance(res, list)
        assert all(len(r) == 2 for r in res)

    def test_key_is_YYYY_MM(self):
        dates = ["2024-01-10T00:00:00Z", "2024-02-10T00:00:00Z"]
        res = rust_backend.emi_monthly_pressure(dates, [500_000, 300_000], [10.0, 12.0], [60, 48])
        keys = [r[0] for r in res]
        assert all(len(k) == 7 for k in keys)  # "YYYY-MM"
        assert all("-" in k for k in keys)

    def test_sorted_chronologically(self):
        dates = ["2024-03-01T00:00:00Z", "2024-01-01T00:00:00Z", "2024-02-01T00:00:00Z"]
        res = rust_backend.emi_monthly_pressure(
            dates, [100_000, 100_000, 100_000], [10.0, 10.0, 10.0], [12, 12, 12]
        )
        keys = [r[0] for r in res]
        assert keys == sorted(keys)

    def test_multiple_emis_same_month_are_summed(self):
        dates = ["2024-01-01T00:00:00Z", "2024-01-15T00:00:00Z"]
        res = rust_backend.emi_monthly_pressure(
            dates, [100_000, 100_000], [10.0, 10.0], [12, 12]
        )
        # Both in Jan 2024 — should be summed into one row
        jan_rows = [r for r in res if r[0] == "2024-01"]
        assert len(jan_rows) == 1
        assert jan_rows[0][1] > 0

    def test_emi_value_is_positive_float(self):
        dates = ["2024-01-10T00:00:00Z"]
        res = rust_backend.emi_monthly_pressure(dates, [500_000], [10.0], [60])
        assert isinstance(res[0][1], float)
        assert res[0][1] > 0


# ═══════════════════════════════════════════════════════════════════════════════
# 14. cashflow_forecast
# ═══════════════════════════════════════════════════════════════════════════════

class TestCashflowForecast:

    def test_returns_list_of_4_tuples(self):
        res = rust_backend.cashflow_forecast(
            ["2024-01-01T00:00:00Z"], [50_000], ["Monthly"], 30
        )
        assert all(len(r) == 4 for r in res)

    def test_sorted_by_date(self):
        res = rust_backend.cashflow_forecast(
            ["2024-01-01T00:00:00Z", "2024-01-05T00:00:00Z"],
            [50_000, -10_000],
            ["Monthly", "Weekly"],
            60,
        )
        dates = [r[0] for r in res]
        assert dates == sorted(dates)

    def test_net_equals_income_minus_expense(self):
        res = rust_backend.cashflow_forecast(
            ["2024-01-01T00:00:00Z"], [50_000], ["Monthly"], 30
        )
        for date, income, expense, net in res:
            assert net == pytest.approx(income - expense, abs=0.01)

    def test_income_positive_expense_positive(self):
        res = rust_backend.cashflow_forecast(
            ["2024-01-01T00:00:00Z", "2024-01-02T00:00:00Z"],
            [1000.0, -500.0],
            ["Weekly", "Weekly"],
            14,
        )
        for _, income, expense, _ in res:
            assert income >= 0.0
            assert expense >= 0.0

    def test_empty_input_returns_empty(self):
        res = rust_backend.cashflow_forecast([], [], [], 30)
        assert res == []


# ═══════════════════════════════════════════════════════════════════════════════
# 15. predict_budget_breach
# NOTE: Rust signature is (dates, prices, budget_amount, horizon_days, simulations)
# The existing test_advanced_analytics.py passes start_date/end_date which
# DO NOT EXIST in the Rust function — that test has a bug and will fail.
# This file uses the correct signature.
# ═══════════════════════════════════════════════════════════════════════════════

class TestPredictBudgetBreach:

    def test_returns_3_tuple(self):
        dates = ["2024-01-01T00:00:00Z", "2024-01-05T00:00:00Z"]
        prices = [-5_000, -7_000]
        res = rust_backend.predict_budget_breach(dates, prices, 15_000, 30, 500)
        assert isinstance(res, tuple)
        assert len(res) == 3

    def test_probability_in_0_to_1(self):
        dates = ["2024-01-01T00:00:00Z", "2024-01-05T00:00:00Z"]
        prices = [-5_000, -7_000]
        prob, _, _ = rust_backend.predict_budget_breach(dates, prices, 15_000, 30, 500)
        assert 0.0 <= prob <= 1.0

    def test_high_spending_high_probability(self):
        """Spending 3× budget over 30 days should give high breach probability."""
        dates = ["2024-01-01T00:00:00Z"] * 10
        prices = [-3_000.0] * 10  # large daily expenses
        prob, _, _ = rust_backend.predict_budget_breach(dates, prices, 1_000, 30, 1000)
        assert prob > 0.5

    def test_zero_spending_zero_probability(self):
        prob, expected, p50 = rust_backend.predict_budget_breach([], [], 10_000, 30, 100)
        assert prob == pytest.approx(0.0)
        assert expected == pytest.approx(0.0)
        assert p50 is None

    def test_expected_spend_is_positive(self):
        dates = ["2024-01-01T00:00:00Z", "2024-01-10T00:00:00Z"]
        prices = [-1_000, -2_000]
        _, expected, _ = rust_backend.predict_budget_breach(dates, prices, 50_000, 30, 200)
        assert expected >= 0.0

    def test_p50_days_is_int_or_none(self):
        dates = ["2024-01-01T00:00:00Z"]
        prices = [-500.0]
        _, _, p50 = rust_backend.predict_budget_breach(dates, prices, 10_000, 30, 200)
        assert p50 is None or isinstance(p50, int)

    def test_only_expenses_counted(self):
        """Income transactions (positive prices) must be ignored."""
        dates = ["2024-01-01T00:00:00Z", "2024-01-02T00:00:00Z"]
        prices_with_income   = [50_000.0, -1_000.0]
        prices_expense_only  = [-1_000.0]
        prob_mixed, _, _ = rust_backend.predict_budget_breach(
            dates, prices_with_income, 5_000, 30, 500
        )
        prob_expense, _, _ = rust_backend.predict_budget_breach(
            [dates[1]], prices_expense_only, 5_000, 30, 500
        )
        # Probabilities should be similar since income is ignored
        assert abs(prob_mixed - prob_expense) < 0.3


# ═══════════════════════════════════════════════════════════════════════════════
# 16. detect_recurring_anomalies
# ═══════════════════════════════════════════════════════════════════════════════

class TestDetectRecurringAnomalies:

    def test_returns_list_of_2_tuples(self):
        dates = ["2024-01-01T00:00:00Z", "2024-02-01T00:00:00Z"]
        prices = [-1000.0, -1000.0]
        res = rust_backend.detect_recurring_anomalies(dates, prices, 30, 0.2)
        assert isinstance(res, list)
        assert all(len(r) == 2 for r in res)

    def test_detects_missed_recurrence(self):
        dates = [
            "2024-01-01T00:00:00Z",
            "2024-01-31T00:00:00Z",
            "2024-03-10T00:00:00Z",  # gap > 30 + 2 days
        ]
        prices = [-1_000.0, -1_000.0, -1_000.0]
        res = rust_backend.detect_recurring_anomalies(dates, prices, 30, 0.3)
        messages = [r[1] for r in res]
        assert any("Missed" in m for m in messages)

    def test_detects_amount_anomaly(self):
        dates = [
            "2024-01-01T00:00:00Z",
            "2024-02-01T00:00:00Z",
            "2024-03-01T00:00:00Z",  # amount 3× normal
        ]
        prices = [-1_000.0, -1_000.0, -3_000.0]
        res = rust_backend.detect_recurring_anomalies(dates, prices, 30, 0.3)
        messages = [r[1] for r in res]
        assert any("Amount" in m for m in messages)

    def test_no_anomalies_for_regular_series(self):
        dates = [
            f"2024-0{m}-01T00:00:00Z" for m in range(1, 7)
        ]
        prices = [-1_000.0] * 6
        res = rust_backend.detect_recurring_anomalies(dates, prices, 30, 0.3)
        # Regular monthly series with consistent amounts — should have no anomalies
        # (gap ≈ 30 days, no amount change)
        assert res == []

    def test_empty_returns_empty(self):
        res = rust_backend.detect_recurring_anomalies([], [], 30, 0.2)
        assert res == []


# ═══════════════════════════════════════════════════════════════════════════════
# 17. income_stability  (previously untested)
# ═══════════════════════════════════════════════════════════════════════════════

class TestIncomeStability:

    def test_returns_2_tuple(self):
        res = rust_backend.income_stability([50_000, 52_000, 48_000])
        assert isinstance(res, tuple)
        assert len(res) == 2

    def test_perfectly_stable_income(self):
        """Zero variance → volatility=0, predictability=100."""
        volatility, predictability = rust_backend.income_stability([50_000] * 12)
        assert volatility == pytest.approx(0.0)
        assert predictability == pytest.approx(100.0)

    def test_predictability_in_0_to_100(self):
        incomes = [50_000, 30_000, 70_000, 10_000, 90_000]
        _, predictability = rust_backend.income_stability(incomes)
        assert 0.0 <= predictability <= 100.0

    def test_volatile_income_low_predictability(self):
        """Wildly varying income should give low predictability."""
        incomes = [100.0, 100_000.0, 50.0, 200_000.0, 1.0]
        _, predictability = rust_backend.income_stability(incomes)
        assert predictability < 50.0

    def test_stable_income_high_predictability(self):
        incomes = [50_000.0, 51_000.0, 49_500.0, 50_200.0]
        _, predictability = rust_backend.income_stability(incomes)
        assert predictability > 80.0

    def test_empty_returns_zeros(self):
        volatility, predictability = rust_backend.income_stability([])
        assert volatility == pytest.approx(0.0)
        assert predictability == pytest.approx(0.0)

    def test_single_income_zero_volatility(self):
        volatility, predictability = rust_backend.income_stability([50_000.0])
        assert volatility == pytest.approx(0.0)
        assert predictability == pytest.approx(100.0)


# ═══════════════════════════════════════════════════════════════════════════════
# 18. savings_metrics  (previously untested)
# ═══════════════════════════════════════════════════════════════════════════════

class TestSavingsMetrics:

    def test_returns_2_tuple(self):
        res = rust_backend.savings_metrics(100_000.0, 70_000.0)
        assert isinstance(res, tuple)
        assert len(res) == 2

    def test_saving_rate_formula(self):
        """saving_rate = (income - expenses) / income × 100."""
        rate, _ = rust_backend.savings_metrics(100_000.0, 70_000.0)
        assert rate == pytest.approx(30.0)

    def test_all_four_score_tiers(self):
        # saving_rate ≥ 30% → score 95
        _, score = rust_backend.savings_metrics(100_000.0, 65_000.0)  # 35%
        assert score == pytest.approx(95.0)

        # 20% ≤ rate < 30% → score 80
        _, score = rust_backend.savings_metrics(100_000.0, 75_000.0)  # 25%
        assert score == pytest.approx(80.0)

        # 10% ≤ rate < 20% → score 60
        _, score = rust_backend.savings_metrics(100_000.0, 85_000.0)  # 15%
        assert score == pytest.approx(60.0)

        # rate < 10% → score 30
        _, score = rust_backend.savings_metrics(100_000.0, 95_000.0)  # 5%
        assert score == pytest.approx(30.0)

    def test_zero_income_returns_zeros(self):
        rate, score = rust_backend.savings_metrics(0.0, 50_000.0)
        assert rate == pytest.approx(0.0)
        assert score == pytest.approx(0.0)

    def test_negative_savings_low_score(self):
        """Spending more than income → negative saving rate → score 30."""
        rate, score = rust_backend.savings_metrics(50_000.0, 60_000.0)
        assert rate < 0.0
        assert score == pytest.approx(30.0)


# ═══════════════════════════════════════════════════════════════════════════════
# 19. net_worth_analysis  (previously untested)
# ═══════════════════════════════════════════════════════════════════════════════

class TestNetWorthAnalysis:

    def test_returns_3_tuple(self):
        res = rust_backend.net_worth_analysis([100_000.0], [30_000.0])
        assert isinstance(res, tuple)
        assert len(res) == 3

    def test_totals_and_net_worth(self):
        assets      = [100_000.0, 50_000.0]
        liabilities = [30_000.0, 20_000.0]
        total_assets, total_liab, net_worth = rust_backend.net_worth_analysis(
            assets, liabilities
        )
        assert total_assets == pytest.approx(150_000.0)
        assert total_liab   == pytest.approx(50_000.0)
        assert net_worth    == pytest.approx(100_000.0)

    def test_net_worth_formula(self):
        total_a, total_l, net = rust_backend.net_worth_analysis(
            [200_000.0], [80_000.0]
        )
        assert net == pytest.approx(total_a - total_l)

    def test_negative_net_worth(self):
        """More liabilities than assets → negative net worth."""
        _, _, net = rust_backend.net_worth_analysis([10_000.0], [50_000.0])
        assert net < 0.0

    def test_empty_inputs(self):
        total_a, total_l, net = rust_backend.net_worth_analysis([], [])
        assert total_a  == pytest.approx(0.0)
        assert total_l  == pytest.approx(0.0)
        assert net      == pytest.approx(0.0)

    def test_single_asset_no_liabilities(self):
        total_a, total_l, net = rust_backend.net_worth_analysis([500_000.0], [])
        assert total_a == pytest.approx(500_000.0)
        assert total_l == pytest.approx(0.0)
        assert net     == pytest.approx(500_000.0)