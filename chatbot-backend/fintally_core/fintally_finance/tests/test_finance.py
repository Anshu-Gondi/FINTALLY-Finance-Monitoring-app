import pytest
import math
import fintally_finance as fi

# ─── helpers ────────────────────────────────────────────────────────────────


def assert_close(received: int, expected: int, tolerance: int = 1):
    diff = abs(received - expected)
    assert diff <= tolerance, (
        f"Expected {received} to be within ±{tolerance} of {expected} (diff={diff})"
    )


def ref_compound(p, r, y, n):
    if p <= 0 or r < 0 or y <= 0 or n <= 0:
        return 0
    return round(p * math.pow(1 + (r / 100) / n, n * y))


def ref_emi(p, r, m):
    if p <= 0 or m <= 0:
        return 0
    mr = (r / 100) / 12
    if mr == 0:
        return round(p / m)
    factor = math.pow(1 + mr, m)
    return round((p * mr * factor) / (factor - 1))


# REPLACE with
def ref_sip(m, r, y, c):
    if m <= 0 or y <= 0 or c <= 0:
        return 0
    total_periods = y * c
    monthly_sum = y * 12
    rate = (r / 100) / c
    if rate == 0:
        return m * monthly_sum
    growth = math.pow(1 + rate, total_periods)
    return round(m * ((growth - 1) / rate) * (1 + rate))
#          the round() wraps everything including (1+rate) — nothing outside it

# ─── compound_interest_batch ─────────────────────────────────────────────────


class TestCompoundInterestBatch:

    def test_length_mismatch_raises(self):
        with pytest.raises(ValueError, match="same length"):
            fi.compound_interest_batch([100, 200], [10.0], [1], [12])

    def test_empty_returns_empty(self):
        assert fi.compound_interest_batch([], [], [], []) == []

    def test_known_formula_annual(self):
        # A = 10000 * (1 + 0.10)^1 = 11000
        result = fi.compound_interest_batch([10000], [10.0], [1], [1])
        assert_close(result[0], 11000)

    def test_known_formula_monthly(self):
        result = fi.compound_interest_batch([10000], [10.0], [1], [12])
        assert_close(result[0], ref_compound(10000, 10, 1, 12))

    def test_quarterly_compounding(self):
        result = fi.compound_interest_batch([100000], [8.0], [5], [4])
        assert_close(result[0], ref_compound(100000, 8, 5, 4))

    def test_daily_compounding(self):
        result = fi.compound_interest_batch([1_000_000], [12.0], [10], [365])
        assert_close(result[0], ref_compound(
            1_000_000, 12, 10, 365), tolerance=2)

    def test_zero_rate_returns_principal(self):
        result = fi.compound_interest_batch([50000], [0.0], [5], [12])
        assert_close(result[0], 50000)

    def test_zero_principal_returns_zero(self):
        assert fi.compound_interest_batch([0], [10.0], [1], [12])[0] == 0

    def test_negative_principal_returns_zero(self):
        assert fi.compound_interest_batch([-1000], [10.0], [1], [12])[0] == 0

    def test_zero_years_returns_zero(self):
        assert fi.compound_interest_batch([10000], [10.0], [0], [12])[0] == 0

    def test_zero_compounds_returns_zero(self):
        assert fi.compound_interest_batch([10000], [10.0], [1], [0])[0] == 0

    def test_mixed_batch_valid_and_invalid(self):
        result = fi.compound_interest_batch(
            [10000, -1, 20000, 0],
            [10.0,  10,   8.0, 5],
            [1,      1,     2, 3],
            [12,    12,     4, 12],
        )
        assert result[1] == 0   # negative principal
        assert result[3] == 0   # zero principal
        assert_close(result[0], ref_compound(10000, 10, 1, 12))
        assert_close(result[2], ref_compound(20000, 8, 2, 4))

    def test_large_batch_consistency(self):
        N = 15_000
        result = fi.compound_interest_batch(
            [10000] * N, [10.0] * N, [1] * N, [12] * N
        )
        single = fi.compound_interest_batch([10000], [10.0], [1], [12])[0]
        assert all(v == single for v in result)

# ─── emi_batch ───────────────────────────────────────────────────────────────


class TestEmiBatch:

    def test_length_mismatch_raises(self):
        with pytest.raises(ValueError):
            fi.emi_batch([100000, 200000], [10.0], [12])

    def test_empty_returns_empty(self):
        assert fi.emi_batch([], [], []) == []

    def test_known_formula(self):
        result = fi.emi_batch([100000], [10.0], [12])
        assert_close(result[0], ref_emi(100000, 10, 12))

    def test_five_year_loan(self):
        result = fi.emi_batch([500000], [8.5], [60])
        assert_close(result[0], ref_emi(500000, 8.5, 60))

    def test_twenty_year_loan(self):
        result = fi.emi_batch([1_000_000], [12.0], [240])
        assert_close(result[0], ref_emi(1_000_000, 12, 240), tolerance=2)

    def test_zero_rate_equal_split(self):
        # 120000 / 12 = 10000 exactly
        result = fi.emi_batch([120000], [0.0], [12])
        assert_close(result[0], 10000)

    def test_zero_principal_returns_zero(self):
        assert fi.emi_batch([0], [10.0], [12])[0] == 0

    def test_negative_principal_returns_zero(self):
        assert fi.emi_batch([-50000], [10.0], [12])[0] == 0

    def test_zero_months_returns_zero(self):
        assert fi.emi_batch([100000], [10.0], [0])[0] == 0

    def test_negative_months_returns_zero(self):
        assert fi.emi_batch([100000], [10.0], [-6])[0] == 0

    def test_batch_matches_individual_calls(self):
        params = [(100000, 10.0, 12), (500000, 8.5, 60), (120000, 0.0, 24)]
        result = fi.emi_batch(
            [p for p, _, _ in params],
            [r for _, r, _ in params],
            [m for _, _, m in params],
        )
        for i, (p, r, m) in enumerate(params):
            assert_close(result[i], ref_emi(p, r, m))

    def test_large_batch_consistency(self):
        N = 15_000
        result = fi.emi_batch([100000] * N, [10.0] * N, [12] * N)
        single = fi.emi_batch([100000], [10.0], [12])[0]
        assert all(v == single for v in result)

# ─── sip_batch ───────────────────────────────────────────────────────────────


class TestSipBatch:

    def test_length_mismatch_raises(self):
        with pytest.raises(ValueError):
            fi.sip_batch([1000, 2000], [12.0], [5], [12])

    def test_empty_returns_empty(self):
        assert fi.sip_batch([], [], [], []) == []

    def test_known_formula_monthly(self):
        result = fi.sip_batch([1000], [12.0], [1], [12])
        assert_close(result[0], ref_sip(1000, 12, 1, 12))

    def test_ten_year_sip(self):
        result = fi.sip_batch([5000], [8.0], [10], [12])
        assert_close(result[0], ref_sip(5000, 8, 10, 12), tolerance=2)

    def test_long_horizon_sip(self):
        result = fi.sip_batch([2000], [15.0], [20], [12])
        assert_close(result[0], ref_sip(2000, 15, 20, 12), tolerance=5)

    def test_zero_rate_pure_sum(self):
        # 1000 * 60 = 60000
        result = fi.sip_batch([1000], [0.0], [5], [12])
        assert_close(result[0], 60000)

    def test_zero_monthly_returns_zero(self):
        assert fi.sip_batch([0], [12.0], [5], [12])[0] == 0

    def test_zero_years_returns_zero(self):
        assert fi.sip_batch([1000], [12.0], [0], [12])[0] == 0

    def test_zero_compounds_returns_zero(self):
        assert fi.sip_batch([1000], [12.0], [5], [0])[0] == 0


    def test_higher_frequency_gives_higher_fv_in_sip(self):
        annual  = fi.sip_batch([1000], [12.0], [5], [1])[0]
        monthly = fi.sip_batch([1000], [12.0], [5], [12])[0]
        # monthly wins because 60 deposits are made vs only 5 annual deposits
        # more capital enters the investment, outweighing the lower per-period rate
        assert monthly > annual

    def test_quarterly_matches_reference(self):
        result = fi.sip_batch([3000], [10.0], [7], [4])
        assert_close(result[0], ref_sip(3000, 10, 7, 4), tolerance=3)

    def test_large_batch_consistency(self):
        N = 15_000
        result = fi.sip_batch([5000] * N, [12.0] * N, [10] * N, [12] * N)
        single = fi.sip_batch([5000], [12.0], [10], [12])[0]
        assert all(v == single for v in result)

# ─── budget_projection_batch ─────────────────────────────────────────────────


class TestBudgetProjectionBatch:

    def test_length_mismatch_raises(self):
        with pytest.raises(ValueError):
            fi.budget_projection_batch([100, 200], [5000], [0], [3])

    def test_return_type_has_correct_fields(self):
        r = fi.budget_projection_batch([1000], [5000], [200], [3])
        assert hasattr(r, "projected_spent")
        assert hasattr(r, "usage_percent")
        assert hasattr(r, "warning_flag")

    def test_projected_spent_math(self):
        # 1000 * 3 + 200 = 3200
        r = fi.budget_projection_batch([1000], [9999], [200], [3])
        assert r.projected_spent[0] == 3200

    def test_projected_spent_zero_months(self):
        # just spent
        r = fi.budget_projection_batch([1000], [9999], [500], [0])
        assert r.projected_spent[0] == 500

    def test_usage_percent_calculation(self):
        # 3200 / 8000 * 100 = 40%
        r = fi.budget_projection_batch([1000], [8000], [200], [3])
        assert abs(r.usage_percent[0] - 40.0) < 0.1

    def test_zero_budget_clamps_to_100(self):
        r = fi.budget_projection_batch([500], [0], [100], [2])
        assert abs(r.usage_percent[0] - 100.0) < 0.001
        assert r.warning_flag[0] == 2

    def test_flag_zero_below_80_percent(self):
        # 100 / 10000 = 1%
        r = fi.budget_projection_batch([100], [10000], [0], [1])
        assert r.warning_flag[0] == 0

    def test_flag_one_at_80_percent(self):
        # 800 / 1000 = 80%
        r = fi.budget_projection_batch([800], [1000], [0], [1])
        assert r.warning_flag[0] == 1

    def test_flag_one_at_99_percent(self):
        r = fi.budget_projection_batch([990], [1000], [0], [1])
        assert r.warning_flag[0] == 1

    def test_flag_two_at_100_percent(self):
        r = fi.budget_projection_batch([1000], [1000], [0], [1])
        assert r.warning_flag[0] == 2

    def test_flag_two_over_budget(self):
        r = fi.budget_projection_batch([1500], [1000], [0], [1])
        assert r.warning_flag[0] == 2
        assert abs(r.usage_percent[0] - 150.0) < 0.1

    def test_all_three_flags_in_one_batch(self):
        r = fi.budget_projection_batch(
            [100,   900,  1100],
            [10000, 1000, 1000],
            [0,     0,    0],
            [1,     1,    1],
        )
        assert r.warning_flag[0] == 0   # 1%
        assert r.warning_flag[1] == 1   # 90%
        assert r.warning_flag[2] == 2   # 110%

    def test_overflow_guard_saturates(self):
        INT64_MAX = 9_223_372_036_854_775_807
        r = fi.budget_projection_batch(
            [9_007_199_254_740_991],  # MAX_SAFE_INTEGER equivalent
            [INT64_MAX],
            [0],
            [1_000_000],
        )
        assert r.projected_spent[0] <= INT64_MAX
        assert r.projected_spent[0] > 0

    def test_large_batch_consistency(self):
        N = 15_000
        r = fi.budget_projection_batch(
            [500] * N,
            [10000] * N,
            [100] * N,
            [4] * N,
        )
        # 500*4 + 100 = 2100; 2100/10000*100 = 21% → flag 0
        assert all(v == 2100 for v in r.projected_spent)
        assert all(abs(v - 21.0) < 0.01 for v in r.usage_percent)
        assert all(v == 0 for v in r.warning_flag)
