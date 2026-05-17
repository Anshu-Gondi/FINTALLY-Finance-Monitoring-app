"""
tests/finance/loans_tests.py
─────────────────────────────────────────────────────────────────────────────
Python-layer tests for loan and EMI tools via execute_tool:
  - calculate_emi
  - assess_loan

What the Rust unit tests already cover (NOT duplicated here):
  - EMI formula correctness (emi.rs)
  - EmiError variants: InvalidPrincipal, InvalidRate, InvalidTenure (emi.rs)
  - is_emi_affordable policy enforcement (emi.rs)
  - assess_loan_checked approval/rejection logic (loans.rs)
  - Joint borrower effective income doubling (loans.rs)
  - Existing EMI reducing available income (loans.rs)
  - Business loan rejection for salaried policy (loans.rs)

What these tests add (the execute_tool FFI boundary):
  - calculate_emi returns {"emi": float} with correct key name
  - EMI value is positive, finite, and in a sensible range
  - assess_loan returns all four fields: approved, max_allowed_emi,
    risk_score, reason
  - approved is a Python bool (not int 0/1 or string)
  - risk_score is a float in [0, 1]
  - reason is a non-empty string on approval
  - Policy string names resolve correctly ("salaried", "self_employed", etc.)
  - Rejection raises RuntimeError (checked path used by execute_tool)
  - Joint borrower flag accepted and processed through JSON
  - Credit score tiers produce correct risk_score values
  - Zero/negative principal raises RuntimeError
  - Missing required fields raise RuntimeError
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


# ── shared fixtures ───────────────────────────────────────────────────────────

SALARIED_POLICY = {
    "emi_policy": {
        "max_emi_percent": 40.0,
        "min_surplus_percent": 30.0,
        "income_type": "Salaried",
        "joint_borrowers": False,
    },
    "allow_business_loans": False,
    "allow_personal_loans": True,
}

JOINT_POLICY = {
    "emi_policy": {
        "max_emi_percent": 45.0,
        "min_surplus_percent": 30.0,
        "income_type": "Salaried",
        "joint_borrowers": True,
    },
    "allow_business_loans": True,
    "allow_personal_loans": True,
}

HIGH_INCOME_POLICY = {
    "emi_policy": {
        "max_emi_percent": 50.0,
        "min_surplus_percent": 25.0,
        "income_type": "Salaried",
        "joint_borrowers": False,
    },
    "allow_business_loans": True,
    "allow_personal_loans": True,
}


def _loan_request(
    income=80_000.0,
    existing_emi=0.0,
    requested_emi=20_000.0,
    credit_score=750,
    purpose="Personal",
    is_joint=False,
):
    return {
        "monthly_income": income,
        "existing_emi": existing_emi,
        "requested_emi": requested_emi,
        "credit_score": credit_score,
        "purpose": purpose,
        "is_joint": is_joint,
    }


# ═══════════════════════════════════════════════════════════════════════════════
# 1. CALCULATE EMI — RETURN SHAPE
# ═══════════════════════════════════════════════════════════════════════════════

class TestCalculateEmiShape:

    def test_result_has_emi_key(self):
        result = call("calculate_emi", principal=100_000.0,
                      annual_rate=12.0, tenure_months=12)
        assert "emi" in result

    def test_emi_is_positive_float(self):
        result = call("calculate_emi", principal=100_000.0,
                      annual_rate=12.0, tenure_months=12)
        assert isinstance(result["emi"], float)
        assert result["emi"] > 0
        assert math.isfinite(result["emi"])

    def test_emi_result_has_no_extra_keys(self):
        """The EMI tool must return exactly {"emi": float}, nothing else."""
        result = call("calculate_emi", principal=100_000.0,
                      annual_rate=10.0, tenure_months=24)
        assert set(result.keys()) == {"emi"}


# ═══════════════════════════════════════════════════════════════════════════════
# 2. CALCULATE EMI — VALUE CONTRACTS
# ═══════════════════════════════════════════════════════════════════════════════

class TestCalculateEmiValues:

    def test_longer_tenure_means_lower_emi(self):
        """Same principal + rate: 240 months EMI < 120 months EMI."""
        short = call("calculate_emi", principal=1_000_000.0,
                     annual_rate=10.0, tenure_months=120)
        long = call("calculate_emi", principal=1_000_000.0,
                    annual_rate=10.0, tenure_months=240)
        assert long["emi"] < short["emi"]

    def test_higher_rate_means_higher_emi(self):
        """Same principal + tenure: 15% rate EMI > 8% rate EMI."""
        low = call("calculate_emi", principal=500_000.0,
                   annual_rate=8.0,  tenure_months=60)
        high = call("calculate_emi", principal=500_000.0,
                    annual_rate=15.0, tenure_months=60)
        assert high["emi"] > low["emi"]

    def test_higher_principal_means_higher_emi(self):
        small = call("calculate_emi", principal=100_000.0,
                     annual_rate=10.0, tenure_months=60)
        large = call("calculate_emi", principal=500_000.0,
                     annual_rate=10.0, tenure_months=60)
        assert large["emi"] > small["emi"]

    def test_emi_sensible_range_home_loan(self):
        """1M at 10% for 20 years should be roughly 9,650."""
        result = call("calculate_emi", principal=1_000_000.0,
                      annual_rate=10.0, tenure_months=240)
        assert 9_000 < result["emi"] < 10_500

    def test_integer_inputs_accepted(self):
        """JSON integers must be accepted where floats are expected."""
        result = call("calculate_emi", principal=100_000,
                      annual_rate=12, tenure_months=12)
        assert result["emi"] > 0


# ═══════════════════════════════════════════════════════════════════════════════
# 3. CALCULATE EMI — ERROR CASES
# ═══════════════════════════════════════════════════════════════════════════════

class TestCalculateEmiErrors:

    def test_zero_principal_raises(self):
        with pytest.raises(RuntimeError):
            call("calculate_emi", principal=0.0,
                 annual_rate=10.0, tenure_months=12)

    def test_negative_principal_raises(self):
        with pytest.raises(RuntimeError):
            call("calculate_emi", principal=-100_000.0,
                 annual_rate=10.0, tenure_months=12)

    def test_zero_rate_raises(self):
        with pytest.raises(RuntimeError):
            call("calculate_emi", principal=100_000.0,
                 annual_rate=0.0, tenure_months=12)

    def test_zero_tenure_raises(self):
        with pytest.raises(RuntimeError):
            call("calculate_emi", principal=100_000.0,
                 annual_rate=10.0, tenure_months=0)

    def test_missing_principal_raises(self):
        with pytest.raises(RuntimeError):
            call("calculate_emi", annual_rate=10.0, tenure_months=12)

    def test_missing_rate_raises(self):
        with pytest.raises(RuntimeError):
            call("calculate_emi", principal=100_000.0, tenure_months=12)


# ═══════════════════════════════════════════════════════════════════════════════
# 4. ASSESS LOAN — RETURN SHAPE
# ═══════════════════════════════════════════════════════════════════════════════

class TestAssessLoanShape:

    def test_approved_loan_has_all_fields(self):
        result = call(
            "assess_loan",
            request=_loan_request(income=80_000.0, requested_emi=20_000.0),
            policy=SALARIED_POLICY,
        )
        assert "approved" in result
        assert "max_allowed_emi" in result
        assert "risk_score" in result
        assert "reason" in result

    def test_approved_is_python_bool(self):
        result = call(
            "assess_loan",
            request=_loan_request(income=80_000.0, requested_emi=20_000.0),
            policy=SALARIED_POLICY,
        )
        assert isinstance(result["approved"], bool), (
            f"approved must be bool, got {type(result['approved'])}"
        )

    def test_max_allowed_emi_is_positive_float(self):
        result = call(
            "assess_loan",
            request=_loan_request(income=80_000.0, requested_emi=20_000.0),
            policy=SALARIED_POLICY,
        )
        assert isinstance(result["max_allowed_emi"], float)
        assert result["max_allowed_emi"] > 0

    def test_risk_score_is_float_in_0_to_1(self):
        result = call(
            "assess_loan",
            request=_loan_request(income=80_000.0, requested_emi=20_000.0),
            policy=SALARIED_POLICY,
        )
        score = result["risk_score"]
        assert isinstance(score, float)
        assert 0.0 <= score <= 1.0

    def test_reason_is_non_empty_string(self):
        result = call(
            "assess_loan",
            request=_loan_request(income=80_000.0, requested_emi=20_000.0),
            policy=SALARIED_POLICY,
        )
        assert isinstance(result["reason"], str)
        assert len(result["reason"]) > 0


# ═══════════════════════════════════════════════════════════════════════════════
# 5. ASSESS LOAN — VALUE CONTRACTS
# ═══════════════════════════════════════════════════════════════════════════════

class TestAssessLoanValues:

    def test_high_credit_score_gives_low_risk(self):
        """Credit score 750-900 → risk_score = 0.1."""
        result = call(
            "assess_loan",
            request=_loan_request(
                income=80_000.0, requested_emi=20_000.0, credit_score=800),
            policy=SALARIED_POLICY,
        )
        assert result["risk_score"] == pytest.approx(0.1)

    def test_mid_credit_score_gives_mid_risk(self):
        """Credit score 650-749 → risk_score = 0.3."""
        result = call(
            "assess_loan",
            request=_loan_request(
                income=80_000.0, requested_emi=20_000.0, credit_score=700),
            policy=SALARIED_POLICY,
        )
        assert result["risk_score"] == pytest.approx(0.3)

    def test_low_credit_score_gives_high_risk(self):
        """Credit score < 650 → risk_score = 0.6."""
        result = call(
            "assess_loan",
            request=_loan_request(
                income=80_000.0, requested_emi=20_000.0, credit_score=600),
            policy=SALARIED_POLICY,
        )
        assert result["risk_score"] == pytest.approx(0.6)

    def test_max_allowed_emi_respects_policy_cap(self):
        """max_allowed_emi must be ≤ income × max_emi_percent / 100."""
        income = 80_000.0
        result = call(
            "assess_loan",
            request=_loan_request(income=income, requested_emi=20_000.0),
            policy=SALARIED_POLICY,
        )
        # Salaried: 40% cap, no existing EMI
        expected_max = income * 0.40
        assert result["max_allowed_emi"] == pytest.approx(
            expected_max, abs=1.0)

    def test_joint_borrower_increases_max_allowed_emi(self):
        """Joint borrower doubles effective income → higher max_allowed_emi."""
        income = 50_000.0
        solo = call(
            "assess_loan",
            request=_loan_request(
                income=income, requested_emi=15_000.0, is_joint=False),
            policy=JOINT_POLICY,
        )
        joint = call(
            "assess_loan",
            request=_loan_request(
                income=income, requested_emi=15_000.0, is_joint=True),
            policy=JOINT_POLICY,
        )
        assert joint["max_allowed_emi"] > solo["max_allowed_emi"]

    def test_existing_emi_reduces_max_allowed(self):
        """Existing EMI reduces effective income → lower max_allowed_emi."""

        no_existing = call(
            "assess_loan",
            request=_loan_request(
                income=80_000.0,
                existing_emi=0.0,
                requested_emi=20_000.0,
            ),
            policy=SALARIED_POLICY,
        )

        with_existing = call(
            "assess_loan",
            request=_loan_request(
                income=80_000.0,
                existing_emi=15_000.0,
                requested_emi=20_000.0,
            ),
            policy=SALARIED_POLICY,
        )

        assert with_existing["max_allowed_emi"] < no_existing["max_allowed_emi"]

    def test_approved_true_for_affordable_loan(self):
        result = call(
            "assess_loan",
            request=_loan_request(
                income=80_000.0, requested_emi=20_000.0, credit_score=750),
            policy=SALARIED_POLICY,
        )
        assert result["approved"] is True


# ═══════════════════════════════════════════════════════════════════════════════
# 6. ASSESS LOAN — ERROR CASES
# ═══════════════════════════════════════════════════════════════════════════════

class TestAssessLoanErrors:

    def test_over_limit_emi_raises(self):
        """EMI exceeding max_emi_percent raises RuntimeError (checked path)."""
        with pytest.raises(RuntimeError):
            call(
                "assess_loan",
                request=_loan_request(income=20_000.0, requested_emi=15_000.0),
                policy=SALARIED_POLICY,
            )

    def test_zero_income_raises(self):
        with pytest.raises(RuntimeError):
            call(
                "assess_loan",
                request=_loan_request(income=0.0, requested_emi=10_000.0),
                policy=SALARIED_POLICY,
            )

    def test_missing_request_field_raises(self):
        with pytest.raises(RuntimeError):
            call("assess_loan", policy=SALARIED_POLICY)

    def test_missing_policy_field_raises(self):
        with pytest.raises(RuntimeError):
            call("assess_loan", request=_loan_request())
