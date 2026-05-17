"""
tests/finance/savings_tests.py
─────────────────────────────────────────────────────────────────────────────
Python-layer tests for savings tools via execute_tool:
  - emergency_fund
  - savings_projection

What the Rust unit tests already cover (NOT duplicated here):
  - emergency_fund formula: expense × months × multiplier (savings.rs)
  - savings_projection compound growth loop (savings.rs)
  - Negative expense rejection (savings.rs)
  - Zero months rejection (savings.rs)

What these tests add (the execute_tool FFI boundary):
  - emergency_fund returns {"emergency_fund": float}
  - Result is positive, finite, scales correctly with inputs
  - Conservative profile (12 months × 1.2) produces correct value
  - savings_projection returns {"projected_savings": float}
  - Zero contribution with zero growth rate returns 0.0
  - Positive growth rate produces more than flat contribution sum
  - Policy struct serialises correctly through JSON
  - Missing required fields raise RuntimeError
  - Negative monthly_expense raises RuntimeError
"""

import json
import math
import pytest

try:
    import fintally_chatbot as fc
except ImportError:
    pytest.skip(
        "fintally_chatbot not built. Run `maturin develop` first.",
        allow_module_level=True,
    )


def _resolve():
    if hasattr(fc, "finance") and hasattr(fc.finance, "execute_tool"):
        return fc.finance.execute_tool
    if hasattr(fc, "execute_tool"):
        return fc.execute_tool
    raise AttributeError(f"Cannot find execute_tool. Available: {dir(fc)}")

_execute_tool = _resolve()


def call(tool: str, **kwargs) -> dict:
    raw = _execute_tool(tool, json.dumps(kwargs))
    result = json.loads(raw)
    assert isinstance(result, dict)
    return result


# ═══════════════════════════════════════════════════════════════════════════════
# 1. EMERGENCY FUND — RETURN SHAPE
# ═══════════════════════════════════════════════════════════════════════════════

class TestEmergencyFundShape:

    def test_result_has_emergency_fund_key(self):
        result = call(
            "emergency_fund",
            monthly_expense=30_000.0,
            policy={"months": 6.0, "expense_multiplier": 1.0},
        )
        assert "emergency_fund" in result

    def test_result_has_exactly_one_key(self):
        result = call(
            "emergency_fund",
            monthly_expense=30_000.0,
            policy={"months": 6.0, "expense_multiplier": 1.0},
        )
        assert set(result.keys()) == {"emergency_fund"}

    def test_value_is_positive_float(self):
        result = call(
            "emergency_fund",
            monthly_expense=20_000.0,
            policy={"months": 6.0, "expense_multiplier": 1.0},
        )
        val = result["emergency_fund"]
        assert isinstance(val, float)
        assert val > 0
        assert math.isfinite(val)


# ═══════════════════════════════════════════════════════════════════════════════
# 2. EMERGENCY FUND — VALUE CONTRACTS
# ═══════════════════════════════════════════════════════════════════════════════

class TestEmergencyFundValues:

    def test_default_policy_6_months(self):
        """6 months × expense × 1.0 multiplier."""
        expense = 30_000.0
        result = call(
            "emergency_fund",
            monthly_expense=expense,
            policy={"months": 6.0, "expense_multiplier": 1.0},
        )
        assert result["emergency_fund"] == pytest.approx(expense * 6.0)

    def test_conservative_policy_12_months_1_2_multiplier(self):
        """12 months × expense × 1.2 multiplier."""
        expense = 20_000.0
        result = call(
            "emergency_fund",
            monthly_expense=expense,
            policy={"months": 12.0, "expense_multiplier": 1.2},
        )
        assert result["emergency_fund"] == pytest.approx(expense * 12.0 * 1.2)

    def test_millionaire_policy_1_month(self):
        """1 month × expense × 1.0."""
        expense = 100_000.0
        result = call(
            "emergency_fund",
            monthly_expense=expense,
            policy={"months": 1.0, "expense_multiplier": 1.0},
        )
        assert result["emergency_fund"] == pytest.approx(expense * 1.0)

    def test_higher_expense_produces_higher_fund(self):
        policy = {"months": 6.0, "expense_multiplier": 1.0}
        low  = call("emergency_fund", monthly_expense=10_000.0, policy=policy)
        high = call("emergency_fund", monthly_expense=50_000.0, policy=policy)
        assert high["emergency_fund"] > low["emergency_fund"]

    def test_higher_multiplier_produces_higher_fund(self):
        expense = 30_000.0
        standard     = call("emergency_fund", monthly_expense=expense,
                            policy={"months": 6.0, "expense_multiplier": 1.0})
        conservative = call("emergency_fund", monthly_expense=expense,
                            policy={"months": 6.0, "expense_multiplier": 1.5})
        assert conservative["emergency_fund"] > standard["emergency_fund"]

    def test_more_months_produces_higher_fund(self):
        expense = 25_000.0
        policy_6  = {"months": 6.0,  "expense_multiplier": 1.0}
        policy_12 = {"months": 12.0, "expense_multiplier": 1.0}
        short = call("emergency_fund", monthly_expense=expense, policy=policy_6)
        long  = call("emergency_fund", monthly_expense=expense, policy=policy_12)
        assert long["emergency_fund"] > short["emergency_fund"]


# ═══════════════════════════════════════════════════════════════════════════════
# 3. EMERGENCY FUND — ERROR CASES
# ═══════════════════════════════════════════════════════════════════════════════

class TestEmergencyFundErrors:

    def test_negative_expense_raises(self):
        with pytest.raises(RuntimeError):
            call(
                "emergency_fund",
                monthly_expense=-10_000.0,
                policy={"months": 6.0, "expense_multiplier": 1.0},
            )

    def test_missing_monthly_expense_raises(self):
        with pytest.raises(RuntimeError):
            call(
                "emergency_fund",
                policy={"months": 6.0, "expense_multiplier": 1.0},
            )

    def test_missing_policy_raises(self):
        with pytest.raises(RuntimeError):
            call("emergency_fund", monthly_expense=30_000.0)

    def test_zero_months_in_policy_raises(self):
        with pytest.raises(RuntimeError):
            call(
                "emergency_fund",
                monthly_expense=30_000.0,
                policy={"months": 0.0, "expense_multiplier": 1.0},
            )


# ═══════════════════════════════════════════════════════════════════════════════
# 4. SAVINGS PROJECTION — RETURN SHAPE
# ═══════════════════════════════════════════════════════════════════════════════

class TestSavingsProjectionShape:

    def test_result_has_projected_savings_key(self):
        result = call(
            "savings_projection",
            months=12,
            policy={"monthly_contribution": 10_000.0, "annual_growth_rate": 0.0},
        )
        assert "projected_savings" in result

    def test_result_has_exactly_one_key(self):
        result = call(
            "savings_projection",
            months=12,
            policy={"monthly_contribution": 10_000.0, "annual_growth_rate": 0.0},
        )
        assert set(result.keys()) == {"projected_savings"}

    def test_value_is_finite_float(self):
        result = call(
            "savings_projection",
            months=12,
            policy={"monthly_contribution": 10_000.0, "annual_growth_rate": 0.08},
        )
        val = result["projected_savings"]
        assert isinstance(val, float)
        assert math.isfinite(val)


# ═══════════════════════════════════════════════════════════════════════════════
# 5. SAVINGS PROJECTION — VALUE CONTRACTS
# ═══════════════════════════════════════════════════════════════════════════════

class TestSavingsProjectionValues:

    def test_zero_contribution_zero_growth_returns_zero(self):
        result = call(
            "savings_projection",
            months=12,
            policy={"monthly_contribution": 0.0, "annual_growth_rate": 0.0},
        )
        assert result["projected_savings"] == pytest.approx(0.0)

    def test_flat_contribution_no_growth(self):
        """12 × 10,000 = 100,000 with no compounding."""
        result = call(
            "savings_projection",
            months=12,
            policy={"monthly_contribution": 10_000.0, "annual_growth_rate": 0.0},
        )
        assert result["projected_savings"] == pytest.approx(120_000.0, abs=1.0)

    def test_growth_rate_increases_projection(self):
        """Same contribution: 8% growth must produce more than 0% growth."""
        flat = call(
            "savings_projection",
            months=12,
            policy={"monthly_contribution": 10_000.0, "annual_growth_rate": 0.0},
        )
        compounded = call(
            "savings_projection",
            months=12,
            policy={"monthly_contribution": 10_000.0, "annual_growth_rate": 0.08},
        )
        assert compounded["projected_savings"] > flat["projected_savings"]

    def test_longer_horizon_produces_more(self):
        policy = {"monthly_contribution": 10_000.0, "annual_growth_rate": 0.08}
        short = call("savings_projection", months=12,  policy=policy)
        long  = call("savings_projection", months=120, policy=policy)
        assert long["projected_savings"] > short["projected_savings"]

    def test_higher_contribution_produces_more(self):
        low  = call("savings_projection", months=12,
                    policy={"monthly_contribution": 5_000.0,  "annual_growth_rate": 0.06})
        high = call("savings_projection", months=12,
                    policy={"monthly_contribution": 20_000.0, "annual_growth_rate": 0.06})
        assert high["projected_savings"] > low["projected_savings"]

    def test_long_term_compound_is_finite(self):
        """120 months compounding at 12% must not overflow."""
        result = call(
            "savings_projection",
            months=120,
            policy={"monthly_contribution": 50_000.0, "annual_growth_rate": 0.12},
        )
        assert math.isfinite(result["projected_savings"])
        assert result["projected_savings"] > 0


# ═══════════════════════════════════════════════════════════════════════════════
# 6. SAVINGS PROJECTION — ERROR CASES
# ═══════════════════════════════════════════════════════════════════════════════

class TestSavingsProjectionErrors:

    def test_zero_months_raises(self):
        with pytest.raises(RuntimeError):
            call(
                "savings_projection",
                months=0,
                policy={"monthly_contribution": 10_000.0, "annual_growth_rate": 0.08},
            )

    def test_missing_months_raises(self):
        with pytest.raises(RuntimeError):
            call(
                "savings_projection",
                policy={"monthly_contribution": 10_000.0, "annual_growth_rate": 0.08},
            )

    def test_missing_policy_raises(self):
        with pytest.raises(RuntimeError):
            call("savings_projection", months=12)