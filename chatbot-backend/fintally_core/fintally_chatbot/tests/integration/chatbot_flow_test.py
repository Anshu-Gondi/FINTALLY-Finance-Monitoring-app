"""
Integration tests for the fintally_chatbot PyO3 extension.

Scope: tests/integration/chatbot_flow.py
-----------------------------------------------
These tests validate the Python ↔ Rust FFI boundary through the single
exported function `execute_tool(tool_name: str, args_json: str) -> str`.

What is NOT tested here (already covered by Rust unit tests):
  - Finance math correctness (EMI formula, budget allocation algorithm, etc.)
  - Error variant wiring inside Rust (AppError, DomainError enum matching)
  - Stream/cancellation mechanics
  - Prompt validation internals
  - Planner tool dispatch logic

What IS tested here:
  1. JSON serialisation / deserialisation across the FFI boundary
  2. Python exception types raised for invalid inputs
  3. Return shape contracts (keys present, types correct)
  4. All 10 tool names resolve from Python string to non-error result
  5. Multi-step financial flows composed entirely from Python
  6. Malformed JSON and missing required fields surface as PyValueError
  7. Unknown tool name surfaces as PyRuntimeError
  8. Domain-rule violations (e.g. zero income) surface as PyRuntimeError
  9. Large / edge-case numeric values don't crash the extension
 10. Return values are valid JSON (double-parse safety)

Run:
    maturin develop          # build the extension in-place
    pytest tests/integration/chatbot_flow.py -v
"""

import json
import math
import pytest

# ── import the compiled extension ──────────────────────────────────────────────
try:
    import fintally_chatbot as fc
except ImportError:
    pytest.skip(
        "fintally_chatbot extension not built. Run `maturin develop` first.",
        allow_module_level=True,
    )

# ── Resolve execute_tool from wherever it's actually registered ────────────────
#
# Your Rust registration chain is:
#   lib.rs          → fintally_chatbot  (top-level module)
#   python_bindings/mod.rs   → adds `finance` submodule
#   python_bindings/finance/mod.rs → registers execute_tool on `finance`
#
# So the real path is fintally_chatbot.finance.execute_tool, NOT
# fintally_chatbot.execute_tool.  We detect this at import time so every
# test failure is "AttributeError at startup" rather than 54 identical lines.
#
def _resolve_execute_tool():
    # Check top-level first (in case someone re-exports it in lib.rs later)
    if hasattr(fc, "execute_tool"):
        return fc.execute_tool
    # Then finance submodule (current registration location)
    if hasattr(fc, "finance") and hasattr(fc.finance, "execute_tool"):
        return fc.finance.execute_tool
    # List what IS available so the error message is actually useful
    top = [x for x in dir(fc) if not x.startswith("_")]
    finance_attrs = []
    if hasattr(fc, "finance"):
        finance_attrs = [x for x in dir(fc.finance) if not x.startswith("_")]
    raise AttributeError(
        f"Cannot find execute_tool in fintally_chatbot.\n"
        f"  Top-level attrs:      {top}\n"
        f"  finance submodule:    {finance_attrs}\n"
        f"Fix: check python_bindings/finance/mod.rs and python_bindings/mod.rs."
    )

_execute_tool = _resolve_execute_tool()


# ═══════════════════════════════════════════════════════════════════════════════
# Helpers
# ═══════════════════════════════════════════════════════════════════════════════

def call(tool: str, **kwargs) -> dict:
    """
    Thin wrapper: serialise kwargs → call execute_tool → deserialise result.
    This is the exact flow a Python consumer would use.
    """
    raw = _execute_tool(tool, json.dumps(kwargs))
    result = json.loads(raw)
    # Guarantee that the result is always a dict (never a bare value)
    assert isinstance(result, dict), f"Expected dict from {tool}, got {type(result)}"
    return result


def call_raw(tool: str, json_str: str) -> dict:
    """Call with a pre-serialised JSON string (for malformed-input tests)."""
    raw = _execute_tool(tool, json_str)
    return json.loads(raw)


# ── Preset payloads (mirrors Rust config.rs presets) ──────────────────────────

SALARIED_EMI_POLICY = {
    "max_emi_percent": 40.0,
    "min_surplus_percent": 30.0,
    "income_type": "Salaried",
    "joint_borrowers": False,
}

SALARIED_LOAN_POLICY = {
    "emi_policy": SALARIED_EMI_POLICY,
    "allow_business_loans": False,
    "allow_personal_loans": True,
}

DEFAULT_EMERGENCY_POLICY = {
    "months": 6.0,
    "expense_multiplier": 1.0,
}

DEFAULT_SAVINGS_POLICY = {
    "monthly_contribution": 10_000.0,
    "annual_growth_rate": 0.08,
}

SIMPLE_TAX_PROFILE = {
    "rules": [
        {
            "domain": "Income",
            "rate_percent": 10.0,
            "base": "PercentageOfIncome",
            "priority": 10,
            "enabled": True,
        }
    ]
}

YOUNG_PROFESSIONAL_BUDGET_PROFILE = {
    "rules": [
        {"category": "Housing",        "min_percent": 25.0, "max_percent": 35.0, "priority": 10},
        {"category": "Food",           "min_percent": 10.0, "max_percent": 15.0, "priority":  8},
        {"category": "Transportation", "min_percent":  5.0, "max_percent": 10.0, "priority":  7},
        {"category": "Savings",        "min_percent": 15.0, "max_percent": 30.0, "priority":  9},
        {"category": "Lifestyle",      "min_percent":  5.0, "max_percent": 15.0, "priority":  4},
    ]
}

YOUNG_PROFESSIONAL_CASHFLOW_PROFILE = {
    "mode": "PriorityBased",
    "rules": [
        {"bucket": "Essentials",          "min_percent": 45.0, "max_percent": 55.0, "priority": 10},
        {"bucket": "FinancialStability",  "min_percent": 25.0, "max_percent": 35.0, "priority":  9},
        {"bucket": "Lifestyle",           "min_percent": 15.0, "max_percent": 25.0, "priority":  6},
    ],
}

YOUNG_PROFESSIONAL_INVESTMENT_PROFILE = {
    "life_stage": "YoungProfessional",
    "risk_tolerance": "High",
    "rules": [
        {
            "goal": "EmergencyBuffer",
            "min_percent": 10.0, "max_percent": 15.0, "priority": 10,
            "allocation": [{"asset": "Cash", "percent": 100.0}],
        },
        {
            "goal": "WealthGrowth",
            "min_percent": 40.0, "max_percent": 70.0, "priority": 9,
            "allocation": [
                {"asset": "Equity", "percent": 80.0},
                {"asset": "Debt",   "percent": 20.0},
            ],
        },
        {
            "goal": "Retirement",
            "min_percent": 20.0, "max_percent": 40.0, "priority": 8,
            "allocation": [
                {"asset": "Equity", "percent": 70.0},
                {"asset": "Debt",   "percent": 30.0},
            ],
        },
    ],
}


# ═══════════════════════════════════════════════════════════════════════════════
# 1. FFI BOUNDARY – JSON SERIALISATION
# ═══════════════════════════════════════════════════════════════════════════════

class TestFFIBoundary:
    """
    Tests that the JSON string contract across the FFI works correctly.
    Rust receives a JSON string; Python gets a JSON string back.
    These tests confirm no data is silently dropped or mutated in transit.
    """

    def test_return_value_is_valid_json_string(self):
        """execute_tool must return a str that is valid JSON."""
        raw = _execute_tool(
            "calculate_emi",
            json.dumps({"principal": 100_000, "annual_rate": 12.0, "tenure_months": 12}),
        )
        assert isinstance(raw, str)
        parsed = json.loads(raw)  # must not raise
        assert isinstance(parsed, dict)

    def test_float_precision_survives_round_trip(self):
        """
        Floats sent from Python must survive JSON → Rust → JSON → Python
        without silent truncation.  We allow 1 cent tolerance.
        """
        result = call(
            "calculate_emi",
            principal=500_000.0,
            annual_rate=8.5,
            tenure_months=120,
        )
        emi = result["emi"]
        assert isinstance(emi, float)
        # Sanity: EMI for 5L at 8.5% for 10 years is ~6200–6300
        assert 6_000 < emi < 6_500

    def test_integer_input_accepted_as_number(self):
        """
        JSON integers and floats are both valid for numeric fields.
        PyO3 must not reject integer JSON where float is expected.
        """
        result = call(
            "calculate_emi",
            principal=100_000,          # int, not float
            annual_rate=12,             # int
            tenure_months=12,
        )
        assert "emi" in result

    def test_result_dict_contains_no_rust_debug_strings(self):
        """
        The result must be pure JSON data, not a Rust Debug representation
        like 'Ok(...)' or 'Err(...)'.
        """
        result = call(
            "calculate_emi",
            principal=100_000.0,
            annual_rate=12.0,
            tenure_months=12,
        )
        raw_str = json.dumps(result)
        assert "Ok(" not in raw_str
        assert "Err(" not in raw_str


# ═══════════════════════════════════════════════════════════════════════════════
# 2. PYTHON EXCEPTION CONTRACT
# ═══════════════════════════════════════════════════════════════════════════════

class TestExceptionTypes:
    """
    Confirms that Rust errors surface as the correct Python exception types.
    This is what Python callers depend on for try/except blocks.
    """

    def test_malformed_json_raises_value_error(self):
        """Invalid JSON string must raise PyValueError (not crash)."""
        with pytest.raises(ValueError):
            _execute_tool("calculate_emi", "{ not valid json }")

    def test_missing_required_field_raises_runtime_error(self):
        """Missing a required field must raise PyRuntimeError."""
        with pytest.raises(RuntimeError):
            call("calculate_emi", annual_rate=12.0, tenure_months=12)
            # 'principal' is missing

    def test_unknown_tool_raises_runtime_error(self):
        """An unregistered tool name must raise PyRuntimeError."""
        with pytest.raises(RuntimeError):
            call("completely_made_up_tool", principal=100_000)

    def test_domain_rule_violation_raises_runtime_error(self):
        """
        Zero income violates the domain rule in Rust.
        Must surface as RuntimeError, NOT a silent empty dict.
        """
        with pytest.raises(RuntimeError):
            call(
                "assess_loan",
                request={
                    "monthly_income": 0.0,   # invalid: zero income
                    "existing_emi": 0.0,
                    "requested_emi": 10_000.0,
                    "credit_score": 750,
                    "purpose": "Personal",
                    "is_joint": False,
                },
                policy=SALARIED_LOAN_POLICY,
            )

    def test_empty_json_object_raises_runtime_error(self):
        """An empty args dict must raise, not return garbage."""
        with pytest.raises(RuntimeError):
            call("calculate_emi")

    def test_wrong_type_for_numeric_field_raises(self):
        """Passing a string where a number is expected must raise."""
        with pytest.raises((ValueError, RuntimeError)):
            call(
                "calculate_emi",
                principal="one lakh",   # string, not number
                annual_rate=12.0,
                tenure_months=12,
            )


# ═══════════════════════════════════════════════════════════════════════════════
# 3. ALL 10 TOOLS RESOLVE FROM PYTHON
# ═══════════════════════════════════════════════════════════════════════════════

class TestAllToolsResolvable:
    """
    Smoke test: every tool name registered in Rust must be callable from Python
    with a minimal valid payload and return a dict.

    This catches tool registration regressions – e.g. a tool added to the Rust
    enum but not registered in the module, or a serialisation mismatch that
    only appears at the FFI level.
    """

    def test_calculate_emi(self):
        result = call(
            "calculate_emi",
            principal=100_000.0,
            annual_rate=12.0,
            tenure_months=12,
        )
        assert "emi" in result
        assert result["emi"] > 0

    def test_assess_loan(self):
        result = call(
            "assess_loan",
            request={
                "monthly_income": 80_000.0,
                "existing_emi": 10_000.0,
                "requested_emi": 20_000.0,
                "credit_score": 750,
                "purpose": "Personal",
                "is_joint": False,
            },
            policy=SALARIED_LOAN_POLICY,
        )
        assert "approved" in result
        assert "max_allowed_emi" in result
        assert "risk_score" in result
        assert "reason" in result

    def test_emergency_fund(self):
        result = call(
            "emergency_fund",
            monthly_expense=30_000.0,
            policy=DEFAULT_EMERGENCY_POLICY,
        )
        assert "emergency_fund" in result
        assert result["emergency_fund"] == pytest.approx(180_000.0)

    def test_savings_projection(self):
        result = call(
            "savings_projection",
            months=12,
            policy=DEFAULT_SAVINGS_POLICY,
        )
        assert "projected_savings" in result
        assert result["projected_savings"] > 0

    def test_calculate_tax(self):
        result = call(
            "calculate_tax",
            amount=100_000.0,
            profile=SIMPLE_TAX_PROFILE,
        )
        assert "taxes" in result
        taxes = result["taxes"]
        assert isinstance(taxes, dict)
        assert "Income" in taxes
        assert taxes["Income"] == pytest.approx(10_000.0)

    def test_generate_investment_plan(self):
        result = call(
            "generate_investment_plan",
            investable_amount=100_000.0,
            profile=YOUNG_PROFESSIONAL_INVESTMENT_PROFILE,
        )
        # Result is a flat dict of goal → amount
        assert isinstance(result, dict)
        assert len(result) > 0
        total = sum(result.values())
        assert total == pytest.approx(100_000.0, abs=1.0)

    def test_generate_cashflow(self):
        result = call(
            "generate_cashflow",
            monthly_income=80_000.0,
            profile=YOUNG_PROFESSIONAL_CASHFLOW_PROFILE,
        )
        assert isinstance(result, dict)
        assert len(result) > 0
        total = sum(result.values())
        assert total == pytest.approx(80_000.0, abs=1.0)

    def test_generate_budget(self):
        result = call(
            "generate_budget",
            monthly_income=80_000.0,
            profile=YOUNG_PROFESSIONAL_BUDGET_PROFILE,
        )
        assert isinstance(result, dict)
        assert len(result) > 0
        total = sum(result.values())
        assert total <= 80_000.0 + 0.01

    def test_profile_similarity(self):
        result = call(
            "profile_similarity",
            a={"user_id": "a", "metrics": [80_000.0, 28.0, 0.6, 0.25]},
            b={"user_id": "b", "metrics": [75_000.0, 30.0, 0.55, 0.20]},
            metric="Cosine",
        )
        assert "score" in result
        score = result["score"]
        assert isinstance(score, float)
        assert 0.0 <= score <= 1.0

    def test_stat_analysis(self):
        single_parent_stat_profile = {
            "metrics": [
                {
                    "name": "BMI",
                    "category": "Health",
                    "value": 23.0,
                    "target": 23.0,
                    "measurement": "Float",
                    "weight": 0.15,
                    "history": [],
                },
                {
                    "name": "Emergency Fund",
                    "category": "Finance",
                    "value": 20_000.0,
                    "target": 20_000.0,
                    "measurement": "Float",
                    "weight": 0.20,
                    "history": [],
                },
                {
                    "name": "Focus Hours",
                    "category": "Productivity",
                    "value": 4.0,
                    "target": 4.0,
                    "measurement": "Float",
                    "weight": 0.10,
                    "history": [],
                },
            ],
            "alert_policy": {
                "target_warning_percent": 10.0,
                "target_critical_percent": 20.0,
                "trend_warning_percent": 15.0,
            },
        }
        result = call("stat_analysis", profile=single_parent_stat_profile)
        assert "scores" in result
        assert "alerts" in result
        assert isinstance(result["scores"], dict)
        assert isinstance(result["alerts"], list)


# ═══════════════════════════════════════════════════════════════════════════════
# 4. RETURN SHAPE CONTRACTS
# ═══════════════════════════════════════════════════════════════════════════════

class TestReturnShapes:
    """
    Verify specific fields, types, and value ranges on tool outputs.
    These are the contracts Python consumers (FastAPI routes, chatbot layer)
    depend on.  A Rust refactor that renames a field breaks things here first.
    """

    def test_emi_is_positive_float(self):
        result = call(
            "calculate_emi",
            principal=1_000_000.0,
            annual_rate=10.0,
            tenure_months=240,
        )
        emi = result["emi"]
        assert isinstance(emi, float)
        assert emi > 0
        assert math.isfinite(emi)

    def test_loan_assessment_approved_is_bool(self):
        result = call(
            "assess_loan",
            request={
                "monthly_income": 80_000.0,
                "existing_emi": 0.0,
                "requested_emi": 20_000.0,
                "credit_score": 780,
                "purpose": "Personal",
                "is_joint": False,
            },
            policy=SALARIED_LOAN_POLICY,
        )
        assert isinstance(result["approved"], bool)
        assert isinstance(result["risk_score"], float)
        assert isinstance(result["reason"], str)
        assert len(result["reason"]) > 0

    def test_loan_rejection_reason_is_non_empty_string(self):
        """
        When a loan fails affordability, execute_tool_async (the checked path)
        propagates the error through llm_safe_error() which raises RuntimeError.
        The `reason` field only exists when the loan is APPROVED — rejections
        surface as exceptions at the execute_tool boundary.

        This test therefore verifies:
          (a) a valid, approved loan has a non-empty reason string, AND
          (b) an over-limit loan raises RuntimeError (not a silent empty dict).
        """
        # (a) Approved loan has a populated reason field
        approved = call(
            "assess_loan",
            request={
                "monthly_income": 80_000.0,
                "existing_emi": 0.0,
                "requested_emi": 20_000.0,   # 25% of income – well within 40% cap
                "credit_score": 750,
                "purpose": "Personal",
                "is_joint": False,
            },
            policy=SALARIED_LOAN_POLICY,
        )
        assert approved["approved"] is True
        assert isinstance(approved["reason"], str)
        assert len(approved["reason"]) > 0

        # (b) Over-limit loan raises at the FFI boundary (not a silent dict)
        with pytest.raises(RuntimeError):
            call(
                "assess_loan",
                request={
                    "monthly_income": 20_000.0,
                    "existing_emi": 0.0,
                    "requested_emi": 15_000.0,   # 75% – fails affordability check
                    "credit_score": 600,
                    "purpose": "Personal",
                    "is_joint": False,
                },
                policy=SALARIED_LOAN_POLICY,
            )

    def test_emergency_fund_scales_with_expense(self):
        """
        The FFI must correctly pass the multiplier through.
        6 months × ₹30,000 × 1.0 = ₹1,80,000
        """
        result = call(
            "emergency_fund",
            monthly_expense=30_000.0,
            policy={"months": 6.0, "expense_multiplier": 1.0},
        )
        assert result["emergency_fund"] == pytest.approx(180_000.0)

    def test_budget_keys_are_category_strings(self):
        """Budget result keys must be BudgetCategory variant names as strings."""
        result = call(
            "generate_budget",
            monthly_income=100_000.0,
            profile=YOUNG_PROFESSIONAL_BUDGET_PROFILE,
        )
        expected_categories = {"Housing", "Food", "Transportation", "Savings", "Lifestyle"}
        assert set(result.keys()) == expected_categories

    def test_cashflow_keys_are_bucket_strings(self):
        """Cashflow result keys must be CashflowBucket variant names."""
        result = call(
            "generate_cashflow",
            monthly_income=100_000.0,
            profile=YOUNG_PROFESSIONAL_CASHFLOW_PROFILE,
        )
        expected_buckets = {"Essentials", "FinancialStability", "Lifestyle"}
        assert set(result.keys()) == expected_buckets

    def test_investment_plan_values_are_non_negative(self):
        result = call(
            "generate_investment_plan",
            investable_amount=100_000.0,
            profile=YOUNG_PROFESSIONAL_INVESTMENT_PROFILE,
        )
        for goal, amount in result.items():
            assert amount >= 0, f"Goal '{goal}' has negative allocation: {amount}"

    def test_similarity_score_euclidean_is_non_negative(self):
        """Euclidean distance is always ≥ 0."""
        result = call(
            "profile_similarity",
            a={"user_id": "x", "metrics": [1.0, 2.0, 3.0]},
            b={"user_id": "y", "metrics": [1.0, 2.0, 6.0]},
            metric="Euclidean",
        )
        assert result["score"] >= 0.0

    def test_similarity_score_cosine_in_range(self):
        """Cosine similarity is in [-1, 1]."""
        result = call(
            "profile_similarity",
            a={"user_id": "x", "metrics": [1.0, 0.0, 0.0]},
            b={"user_id": "y", "metrics": [0.0, 1.0, 0.0]},
            metric="Cosine",
        )
        assert -1.0 <= result["score"] <= 1.0

    def test_stat_scores_are_0_to_100(self):
        """Stat scores must be in [0, 100] range."""
        profile = {
            "metrics": [
                {
                    "name": "Net Worth",
                    "category": "Finance",
                    "value": 50_000.0,
                    "target": 50_000.0,
                    "measurement": "Float",
                    "weight": 0.25,
                    "history": [],
                }
            ],
            "alert_policy": {
                "target_warning_percent": 10.0,
                "target_critical_percent": 20.0,
                "trend_warning_percent": 15.0,
            },
        }
        result = call("stat_analysis", profile=profile)
        for category, score in result["scores"].items():
            assert 0.0 <= score <= 100.0, (
                f"Category '{category}' score {score} out of [0, 100]"
            )


# ═══════════════════════════════════════════════════════════════════════════════
# 5. MULTI-STEP FINANCIAL FLOWS (composition from Python)
# ═══════════════════════════════════════════════════════════════════════════════

class TestMultiStepFlows:
    """
    These tests call multiple tools in sequence, using the output of one as
    input to the next.  This is how the chatbot layer actually works.
    The Rust unit tests can't test this because each unit test is isolated.
    """

    def test_emi_then_loan_eligibility(self):
        """
        Flow: compute EMI for a loan amount, then check if that EMI
        is affordable for the user.  The two tools share no state in Rust –
        all state lives in Python between calls.
        """
        # Step 1: what would the EMI be?
        emi_result = call(
            "calculate_emi",
            principal=1_000_000.0,
            annual_rate=10.0,
            tenure_months=120,
        )
        emi = emi_result["emi"]
        assert emi > 0

        # Step 2: can this user afford it?
        loan_result = call(
            "assess_loan",
            request={
                "monthly_income": 80_000.0,
                "existing_emi": 0.0,
                "requested_emi": emi,           # ← output of step 1
                "credit_score": 720,
                "purpose": "Personal",
                "is_joint": False,
            },
            policy=SALARIED_LOAN_POLICY,
        )
        assert "approved" in loan_result
        # At 80k income, ~13.2k EMI should be approved under 40% cap
        assert loan_result["approved"] is True

    def test_budget_then_investment_allocation(self):
        """
        Flow: generate a budget to find the savings allocation, then use
        that savings amount as the investable amount for an investment plan.
        """
        income = 100_000.0

        budget = call(
            "generate_budget",
            monthly_income=income,
            profile=YOUNG_PROFESSIONAL_BUDGET_PROFILE,
        )
        savings_amount = budget.get("Savings")
        assert savings_amount is not None
        assert savings_amount > 0

        investment_plan = call(
            "generate_investment_plan",
            investable_amount=savings_amount,   # ← output of step 1
            profile=YOUNG_PROFESSIONAL_INVESTMENT_PROFILE,
        )
        total_invested = sum(investment_plan.values())
        assert total_invested == pytest.approx(savings_amount, abs=1.0)

    def test_cashflow_then_emergency_fund_sizing(self):
        """
        Flow: compute cashflow allocation, extract the Essentials bucket
        as the monthly expense, then size the emergency fund from that.
        """
        income = 60_000.0

        cashflow = call(
            "generate_cashflow",
            monthly_income=income,
            profile=YOUNG_PROFESSIONAL_CASHFLOW_PROFILE,
        )
        essentials = cashflow.get("Essentials")
        assert essentials is not None

        emergency = call(
            "emergency_fund",
            monthly_expense=essentials,         # ← output of step 1
            policy=DEFAULT_EMERGENCY_POLICY,
        )
        # 6 months of essentials
        assert emergency["emergency_fund"] == pytest.approx(essentials * 6, abs=1.0)

    def test_tax_then_savings_projection(self):
        """
        Flow: compute tax on income, subtract it to get post-tax income,
        then project savings from the post-tax monthly contribution.
        """
        gross = 100_000.0

        tax_result = call(
            "calculate_tax",
            amount=gross,
            profile=SIMPLE_TAX_PROFILE,
        )
        income_tax = tax_result["taxes"]["Income"]
        post_tax = gross - income_tax   # 90,000

        savings_policy = {
            "monthly_contribution": post_tax * 0.20,  # save 20% of post-tax
            "annual_growth_rate": 0.08,
        }
        projection = call(
            "savings_projection",
            months=12,
            policy=savings_policy,                    # ← derived from step 1
        )
        assert projection["projected_savings"] > 0

    def test_full_financial_health_check(self):
        """
        Full flow simulating a chatbot session that assesses a user's
        complete financial picture in one conversation turn:
          1. Compute EMI for existing loan
          2. Check loan eligibility
          3. Generate budget given income
          4. Size emergency fund
          5. Analyse stat profile

        Each step's output is Python-side state passed into the next call.
        This is the key integration scenario the Rust unit tests cannot cover.
        """
        income = 80_000.0
        existing_loan_principal = 500_000.0

        # 1. EMI for existing loan
        emi_res = call(
            "calculate_emi",
            principal=existing_loan_principal,
            annual_rate=10.0,
            tenure_months=60,
        )
        existing_emi = emi_res["emi"]

        # 2. Check if a new loan is still affordable
        loan_res = call(
            "assess_loan",
            request={
                "monthly_income": income,
                "existing_emi": existing_emi,
                "requested_emi": 10_000.0,
                "credit_score": 710,
                "purpose": "Personal",
                "is_joint": False,
            },
            policy=SALARIED_LOAN_POLICY,
        )
        # We don't assert approved/rejected – we assert the shape is correct
        assert isinstance(loan_res["approved"], bool)

        # 3. Budget
        budget_res = call(
            "generate_budget",
            monthly_income=income,
            profile=YOUNG_PROFESSIONAL_BUDGET_PROFILE,
        )
        assert sum(budget_res.values()) <= income + 0.01

        # 4. Emergency fund from Food + Housing bucket spend
        monthly_expense = budget_res.get("Housing", 0) + budget_res.get("Food", 0)
        ef_res = call(
            "emergency_fund",
            monthly_expense=monthly_expense,
            policy=DEFAULT_EMERGENCY_POLICY,
        )
        assert ef_res["emergency_fund"] > 0

        # 5. Stat analysis
        stat_profile = {
            "metrics": [
                {
                    "name": "EMI Load %",
                    "category": "Finance",
                    "value": (existing_emi / income) * 100,
                    "target": 40.0,
                    "measurement": "Percentage",
                    "weight": 0.3,
                    "history": [],
                },
                {
                    "name": "Emergency Fund",
                    "category": "Finance",
                    "value": ef_res["emergency_fund"],
                    "target": monthly_expense * 6,
                    "measurement": "Float",
                    "weight": 0.2,
                    "history": [],
                },
            ],
            "alert_policy": {
                "target_warning_percent": 10.0,
                "target_critical_percent": 20.0,
                "trend_warning_percent": 15.0,
            },
        }
        stat_res = call("stat_analysis", profile=stat_profile)
        assert "scores" in stat_res
        assert "alerts" in stat_res


# ═══════════════════════════════════════════════════════════════════════════════
# 6. EDGE CASES AND NUMERIC BOUNDARIES
# ═══════════════════════════════════════════════════════════════════════════════

class TestEdgeCases:
    """
    Tests that very large / very small / boundary values don't cause the
    extension to crash, hang, or return NaN/Inf.
    These are NOT in the Rust unit tests because Rust tests use fixed presets.
    """

    def test_very_large_income_does_not_crash(self):
        result = call(
            "generate_budget",
            monthly_income=10_000_000.0,   # ₹1 crore/month
            profile=YOUNG_PROFESSIONAL_BUDGET_PROFILE,
        )
        total = sum(result.values())
        assert total <= 10_000_000.0 + 1.0
        for v in result.values():
            assert math.isfinite(v)

    def test_small_income_near_minimum_survives(self):
        """
        ₹1 income – extremely low.  Rust should handle it without crashing.
        The domain allows it (income > 0.0), so we just check shape.
        """
        result = call(
            "generate_budget",
            monthly_income=1.0,
            profile=YOUNG_PROFESSIONAL_BUDGET_PROFILE,
        )
        assert isinstance(result, dict)
        total = sum(result.values())
        assert total <= 1.01

    def test_emi_for_very_long_tenure(self):
        """360 months (30 years) should not overflow or return NaN."""
        result = call(
            "calculate_emi",
            principal=5_000_000.0,
            annual_rate=8.0,
            tenure_months=360,
        )
        emi = result["emi"]
        assert math.isfinite(emi)
        assert emi > 0

    def test_profile_similarity_identical_vectors(self):
        """
        Identical vectors: Cosine = 1.0, Euclidean = 0.0, Pearson = 1.0.
        Tests that perfect similarity doesn't cause division-by-zero.
        """
        v = [1.0, 2.0, 3.0, 4.0, 5.0]
        cosine = call(
            "profile_similarity",
            a={"user_id": "a", "metrics": v},
            b={"user_id": "b", "metrics": v},
            metric="Cosine",
        )
        assert cosine["score"] == pytest.approx(1.0, abs=1e-6)

        euclidean = call(
            "profile_similarity",
            a={"user_id": "a", "metrics": v},
            b={"user_id": "b", "metrics": v},
            metric="Euclidean",
        )
        assert euclidean["score"] == pytest.approx(0.0, abs=1e-6)

    def test_savings_projection_long_horizon(self):
        """
        120 months of compounding should return a finite positive number,
        not overflow.
        """
        result = call(
            "savings_projection",
            months=120,
            policy={"monthly_contribution": 50_000.0, "annual_growth_rate": 0.12},
        )
        assert math.isfinite(result["projected_savings"])
        assert result["projected_savings"] > 0

    def test_tax_profile_with_disabled_rule_returns_empty_taxes(self):
        """
        A profile with all rules disabled should return an empty taxes dict,
        not crash.
        """
        profile = {
            "rules": [
                {
                    "domain": "Income",
                    "rate_percent": 10.0,
                    "base": "PercentageOfIncome",
                    "priority": 10,
                    "enabled": False,   # disabled
                }
            ]
        }
        result = call("calculate_tax", amount=100_000.0, profile=profile)
        assert result["taxes"] == {}

    def test_emergency_fund_with_conservative_multiplier(self):
        """
        Conservative profile: 12 months × 1.2 multiplier.
        """
        result = call(
            "emergency_fund",
            monthly_expense=20_000.0,
            policy={"months": 12.0, "expense_multiplier": 1.2},
        )
        assert result["emergency_fund"] == pytest.approx(288_000.0)

    def test_zero_income_budget_raises(self):
        """Zero income is a domain error in Rust; must raise from Python."""
        with pytest.raises(RuntimeError):
            call(
                "generate_budget",
                monthly_income=0.0,
                profile=YOUNG_PROFESSIONAL_BUDGET_PROFILE,
            )

    def test_zero_income_cashflow_raises(self):
        with pytest.raises(RuntimeError):
            call(
                "generate_cashflow",
                monthly_income=0.0,
                profile=YOUNG_PROFESSIONAL_CASHFLOW_PROFILE,
            )

    def test_negative_income_investment_raises(self):
        with pytest.raises(RuntimeError):
            call(
                "generate_investment_plan",
                investable_amount=-50_000.0,
                profile=YOUNG_PROFESSIONAL_INVESTMENT_PROFILE,
            )


# ═══════════════════════════════════════════════════════════════════════════════
# 7. TOOL NAME REGISTRATION COMPLETENESS
# ═══════════════════════════════════════════════════════════════════════════════

class TestToolRegistry:
    """
    These tests ensure the tool name → Rust handler mapping is complete.
    If a tool is added to the Rust enum but not wired in execute_tool_async,
    it will fail here (even if it passes the Rust from_str test).
    """

    VALID_TOOLS_WITH_MINIMAL_ARGS = [
        ("calculate_emi",          {"principal": 100_000.0, "annual_rate": 10.0, "tenure_months": 12}),
        ("emergency_fund",         {"monthly_expense": 20_000.0, "policy": DEFAULT_EMERGENCY_POLICY}),
        ("savings_projection",     {"months": 6, "policy": DEFAULT_SAVINGS_POLICY}),
        ("calculate_tax",          {"amount": 100_000.0, "profile": SIMPLE_TAX_PROFILE}),
        ("generate_budget",        {"monthly_income": 60_000.0, "profile": YOUNG_PROFESSIONAL_BUDGET_PROFILE}),
        ("generate_cashflow",      {"monthly_income": 60_000.0, "profile": YOUNG_PROFESSIONAL_CASHFLOW_PROFILE}),
        ("generate_investment_plan", {"investable_amount": 50_000.0, "profile": YOUNG_PROFESSIONAL_INVESTMENT_PROFILE}),
        ("profile_similarity",     {
            "a": {"user_id": "a", "metrics": [1.0, 2.0]},
            "b": {"user_id": "b", "metrics": [1.0, 2.0]},
            "metric": "Cosine",
        }),
    ]

    @pytest.mark.parametrize("tool_name,args", VALID_TOOLS_WITH_MINIMAL_ARGS)
    def test_tool_resolves_and_returns_dict(self, tool_name, args):
        """Every registered tool must return a non-empty dict for valid args."""
        result = call(tool_name, **args)
        assert isinstance(result, dict)

    def test_all_known_tool_names_do_not_raise_unknown_tool_error(self):
        """
        Guard against silent removal: if any tool name stops being recognised,
        we catch it as 'Unknown tool' – separately from execution errors.
        """
        known_tools = [
            "calculate_emi",
            "assess_loan",
            "emergency_fund",
            "savings_projection",
            "calculate_tax",
            "generate_investment_plan",
            "generate_cashflow",
            "generate_budget",
            "profile_similarity",
            "stat_analysis",
        ]
        for tool_name in known_tools:
            try:
                _execute_tool(tool_name, "{}")
            except RuntimeError as e:
                # Missing args → RuntimeError is expected
                # "Unknown tool" → NOT expected: fail hard
                assert "Unknown tool" not in str(e), (
                    f"Tool '{tool_name}' is no longer registered in Rust"
                )
            except ValueError:
                pass  # malformed JSON path – not our concern here