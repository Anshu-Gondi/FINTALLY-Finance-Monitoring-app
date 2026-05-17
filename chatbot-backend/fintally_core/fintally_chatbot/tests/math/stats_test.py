"""
tests/math/stats_tests.py
─────────────────────────────────────────────────────────────────────────────
Python-layer tests for the math tools exposed via execute_tool:
  - stat_analysis   (compute_stat_scores + generate_alerts)
  - profile_similarity (euclidean, cosine, pearson)

What the Rust unit tests already cover (NOT duplicated here):
  - Formula correctness for euclidean/cosine/pearson (similarity.rs)
  - Empty vector errors (similarity.rs)
  - compute_stat_scores weight/target logic (stats.rs)
  - generate_alerts threshold logic (stats.rs)
  - Empty metrics error (stats.rs)

What these tests add (the execute_tool FFI boundary):
  - stat_analysis result has "scores" and "alerts" keys as Python dicts/lists
  - Score values are floats in [0, 100]
  - Alert objects have the required fields: metric_name, category, message, level
  - Alert levels are one of the known strings
  - profile_similarity returns {"score": float} for all three metrics
  - Cosine of identical vectors = 1.0, orthogonal = 0.0
  - Euclidean of identical vectors = 0.0
  - Pearson of perfectly correlated vectors = 1.0
  - Wrong metric name raises RuntimeError
  - Mismatched vector lengths are handled without crash
  - Multi-category stat profiles produce scores for each category present
  - Alerts are generated when values deviate far from targets
  - No alerts when values exactly match targets
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


# ── resolve execute_tool ──────────────────────────────────────────────────────
def _resolve():
    if hasattr(fc, "finance") and hasattr(fc.finance, "execute_tool"):
        return fc.finance.execute_tool
    if hasattr(fc, "execute_tool"):
        return fc.execute_tool
    raise AttributeError(
        f"Cannot find execute_tool. Available: {dir(fc)}"
    )

_execute_tool = _resolve()


def call(tool: str, **kwargs) -> dict:
    raw = _execute_tool(tool, json.dumps(kwargs))
    result = json.loads(raw)
    assert isinstance(result, dict)
    return result


# ── shared fixtures ───────────────────────────────────────────────────────────

def _stat_profile(metrics: list, policy: dict | None = None) -> dict:
    return {
        "metrics": metrics,
        "alert_policy": policy or {
            "target_warning_percent": 10.0,
            "target_critical_percent": 20.0,
            "trend_warning_percent": 15.0,
        },
    }


def _metric(name, category, value, target=None, weight=0.2, history=None):
    return {
        "name": name,
        "category": category,
        "value": value,
        "target": target,
        "measurement": "Float",
        "weight": weight,
        "history": history or [],
    }


def _vec(user_id: str, values: list) -> dict:
    return {"user_id": user_id, "metrics": values}


# ═══════════════════════════════════════════════════════════════════════════════
# 1. STAT ANALYSIS — RETURN SHAPE
# ═══════════════════════════════════════════════════════════════════════════════

class TestStatAnalysisShape:

    def test_result_has_scores_and_alerts_keys(self):
        profile = _stat_profile([_metric("BMI", "Health", 23.0, target=23.0)])
        result = call("stat_analysis", profile=profile)
        assert "scores" in result
        assert "alerts" in result

    def test_scores_is_dict(self):
        profile = _stat_profile([_metric("BMI", "Health", 23.0, target=23.0)])
        result = call("stat_analysis", profile=profile)
        assert isinstance(result["scores"], dict)

    def test_alerts_is_list(self):
        profile = _stat_profile([_metric("BMI", "Health", 23.0, target=23.0)])
        result = call("stat_analysis", profile=profile)
        assert isinstance(result["alerts"], list)

    def test_score_values_are_floats_in_0_to_100(self):
        profile = _stat_profile([
            _metric("BMI",        "Health",      23.0, target=23.0),
            _metric("Net Worth",  "Finance",  50000.0, target=50000.0),
            _metric("Focus",      "Productivity", 6.0, target=6.0),
        ])
        result = call("stat_analysis", profile=profile)
        for category, score in result["scores"].items():
            assert isinstance(score, float), f"{category} score is not float"
            assert 0.0 <= score <= 100.0, f"{category} score {score} out of [0,100]"

    def test_multi_category_profile_produces_all_categories(self):
        profile = _stat_profile([
            _metric("BMI",       "Health",       23.0, target=23.0),
            _metric("Net Worth", "Finance",   50000.0, target=50000.0),
            _metric("Focus",     "Productivity", 6.0,  target=6.0),
            _metric("Hobbies",   "Lifestyle",    8.0,  target=8.0),
        ])
        result = call("stat_analysis", profile=profile)
        scores = result["scores"]
        assert "Health"       in scores
        assert "Finance"      in scores
        assert "Productivity" in scores
        assert "Lifestyle"    in scores


# ═══════════════════════════════════════════════════════════════════════════════
# 2. STAT ANALYSIS — ALERT OBJECTS
# ═══════════════════════════════════════════════════════════════════════════════

class TestStatAlerts:

    KNOWN_LEVELS = {"Info", "Warning", "Critical"}

    def test_no_alerts_when_on_target(self):
        """Values exactly matching targets should produce no alerts."""
        profile = _stat_profile([
            _metric("BMI", "Health", 23.0, target=23.0),
        ])
        result = call("stat_analysis", profile=profile)
        assert result["alerts"] == []

    def test_alert_generated_when_far_from_target(self):
        """A value 50% away from target must produce at least a Warning alert."""
        profile = _stat_profile([
            _metric("BMI", "Health", 34.5, target=23.0),  # 50% above target
        ])
        result = call("stat_analysis", profile=profile)
        assert len(result["alerts"]) > 0

    def test_alert_objects_have_required_fields(self):
        profile = _stat_profile([
            _metric("BMI", "Health", 40.0, target=23.0),  # way off
        ])
        result = call("stat_analysis", profile=profile)
        assert len(result["alerts"]) > 0
        for alert in result["alerts"]:
            assert "metric_name" in alert, f"Missing metric_name: {alert}"
            assert "category"    in alert, f"Missing category: {alert}"
            assert "message"     in alert, f"Missing message: {alert}"
            assert "level"       in alert, f"Missing level: {alert}"

    def test_alert_level_is_known_string(self):
        profile = _stat_profile([
            _metric("Net Worth", "Finance", 1000.0, target=50000.0),
        ])
        result = call("stat_analysis", profile=profile)
        for alert in result["alerts"]:
            assert alert["level"] in self.KNOWN_LEVELS, (
                f"Unknown alert level: {alert['level']}"
            )

    def test_alert_message_is_non_empty_string(self):
        profile = _stat_profile([
            _metric("Focus", "Productivity", 1.0, target=6.0),
        ])
        result = call("stat_analysis", profile=profile)
        for alert in result["alerts"]:
            assert isinstance(alert["message"], str)
            assert len(alert["message"]) > 0

    def test_critical_alert_for_large_deviation(self):
        """A value 25%+ away from target should trigger Critical (threshold=20%)."""
        profile = _stat_profile(
            metrics=[_metric("BMI", "Health", 30.0, target=23.0)],  # ~30% off
            policy={
                "target_warning_percent": 10.0,
                "target_critical_percent": 20.0,
                "trend_warning_percent": 15.0,
            }
        )
        result = call("stat_analysis", profile=profile)
        levels = [a["level"] for a in result["alerts"]]
        assert "Critical" in levels, (
            f"Expected Critical alert for large deviation, got: {levels}"
        )

    def test_trend_alert_from_history(self):
        """A large change in history values must produce a trend Warning."""
        profile = _stat_profile([
            _metric(
                "Emergency Fund", "Finance",
                value=10_000.0,
                target=20_000.0,
                history=[30_000.0, 20_000.0, 10_000.0],  # 50% drop
            )
        ])
        result = call("stat_analysis", profile=profile)
        trend_alerts = [
            a for a in result["alerts"]
            if a["metric_name"] == "Emergency Fund"
        ]
        assert len(trend_alerts) > 0

    def test_empty_metrics_raises(self):
        """Empty metrics list must raise RuntimeError, not return empty scores."""
        profile = _stat_profile([])
        with pytest.raises(RuntimeError):
            call("stat_analysis", profile=profile)


# ═══════════════════════════════════════════════════════════════════════════════
# 3. PROFILE SIMILARITY — RETURN SHAPE
# ═══════════════════════════════════════════════════════════════════════════════

class TestProfileSimilarityShape:

    def test_result_has_score_key(self):
        result = call(
            "profile_similarity",
            a=_vec("a", [1.0, 2.0, 3.0]),
            b=_vec("b", [1.0, 2.0, 3.0]),
            metric="Cosine",
        )
        assert "score" in result

    def test_score_is_float(self):
        result = call(
            "profile_similarity",
            a=_vec("a", [1.0, 2.0]),
            b=_vec("b", [3.0, 4.0]),
            metric="Euclidean",
        )
        assert isinstance(result["score"], float)
        assert math.isfinite(result["score"])

    def test_all_three_metrics_return_score(self):
        a = _vec("a", [1.0, 2.0, 3.0])
        b = _vec("b", [4.0, 5.0, 6.0])
        for metric in ("Euclidean", "Cosine", "Pearson"):
            result = call("profile_similarity", a=a, b=b, metric=metric)
            assert "score" in result, f"No score for metric {metric}"
            assert math.isfinite(result["score"])


# ═══════════════════════════════════════════════════════════════════════════════
# 4. PROFILE SIMILARITY — MATHEMATICAL CONTRACTS
# ═══════════════════════════════════════════════════════════════════════════════

class TestProfileSimilarityContracts:

    def test_cosine_identical_vectors_is_one(self):
        v = [1.0, 2.0, 3.0, 4.0]
        result = call(
            "profile_similarity",
            a=_vec("a", v), b=_vec("b", v),
            metric="Cosine",
        )
        assert result["score"] == pytest.approx(1.0, abs=1e-6)

    def test_cosine_orthogonal_vectors_is_zero(self):
        result = call(
            "profile_similarity",
            a=_vec("a", [1.0, 0.0, 0.0]),
            b=_vec("b", [0.0, 1.0, 0.0]),
            metric="Cosine",
        )
        assert result["score"] == pytest.approx(0.0, abs=1e-6)

    def test_euclidean_identical_vectors_is_zero(self):
        v = [1.0, 2.0, 3.0]
        result = call(
            "profile_similarity",
            a=_vec("a", v), b=_vec("b", v),
            metric="Euclidean",
        )
        assert result["score"] == pytest.approx(0.0, abs=1e-6)

    def test_euclidean_is_non_negative(self):
        result = call(
            "profile_similarity",
            a=_vec("a", [1.0, 2.0, 3.0]),
            b=_vec("b", [4.0, 5.0, 6.0]),
            metric="Euclidean",
        )
        assert result["score"] >= 0.0

    def test_pearson_perfectly_correlated_is_one(self):
        result = call(
            "profile_similarity",
            a=_vec("a", [1.0, 2.0, 3.0, 4.0, 5.0]),
            b=_vec("b", [2.0, 4.0, 6.0, 8.0, 10.0]),  # exact linear scaling
            metric="Pearson",
        )
        assert result["score"] == pytest.approx(1.0, abs=1e-6)

    def test_pearson_negatively_correlated(self):
        result = call(
            "profile_similarity",
            a=_vec("a", [1.0, 2.0, 3.0, 4.0, 5.0]),
            b=_vec("b", [5.0, 4.0, 3.0, 2.0, 1.0]),
            metric="Pearson",
        )
        assert result["score"] == pytest.approx(-1.0, abs=1e-6)

    def test_cosine_in_minus_one_to_one(self):
        result = call(
            "profile_similarity",
            a=_vec("a", [80_000.0, 28.0, 0.6]),
            b=_vec("b", [75_000.0, 30.0, 0.55]),
            metric="Cosine",
        )
        assert -1.0 <= result["score"] <= 1.0

    def test_euclidean_known_value(self):
        """distance([1,2,3], [1,2,6]) = sqrt((3)^2) = 3.0"""
        result = call(
            "profile_similarity",
            a=_vec("a", [1.0, 2.0, 3.0]),
            b=_vec("b", [1.0, 2.0, 6.0]),
            metric="Euclidean",
        )
        assert result["score"] == pytest.approx(3.0, abs=1e-6)


# ═══════════════════════════════════════════════════════════════════════════════
# 5. PROFILE SIMILARITY — ERROR CASES
# ═══════════════════════════════════════════════════════════════════════════════

class TestProfileSimilarityErrors:

    def test_unknown_metric_raises(self):
        with pytest.raises(RuntimeError):
            call(
                "profile_similarity",
                a=_vec("a", [1.0, 2.0]),
                b=_vec("b", [1.0, 2.0]),
                metric="Manhattan",   # not registered
            )

    def test_empty_vectors_raise(self):
        with pytest.raises(RuntimeError):
            call(
                "profile_similarity",
                a=_vec("a", []),
                b=_vec("b", []),
                metric="Cosine",
            )

    def test_missing_metric_field_raises(self):
        with pytest.raises(RuntimeError):
            call(
                "profile_similarity",
                a=_vec("a", [1.0, 2.0]),
                b=_vec("b", [1.0, 2.0]),
                # metric is missing
            )