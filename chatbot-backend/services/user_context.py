"""
services/user_context.py

Fetches a compact snapshot of a user's financial state by calling
analytics_service functions directly (no HTTP round-trip).

This context is injected into the LLM system prompt on every chat request
so the model answers with the user's ACTUAL numbers instead of generic advice.

Design decisions:
  - All fetches run concurrently via asyncio.gather — one DB round-trip cost
  - Each fetch has a hard timeout so a slow analytics call never stalls chat
  - Failures are silently swallowed per-metric — partial context > no context
  - Output is a compact plain-text summary (not JSON) because TinyLlama
    handles natural language context far better than raw JSON in the prompt
"""

import asyncio
import logging
from typing import Optional

logger = logging.getLogger(__name__)

# Timeout per analytics fetch (seconds)
_FETCH_TIMEOUT = 4.0


async def _safe(coro, label: str, default):
    """Run a coroutine with timeout and silent failure."""
    try:
        return await asyncio.wait_for(coro, timeout=_FETCH_TIMEOUT)
    except asyncio.TimeoutError:
        logger.warning(f"user_context: {label} timed out")
        return default
    except Exception as e:
        logger.warning(f"user_context: {label} failed — {e}")
        return default


async def get_user_context(user_id: str) -> str:
    """
    Returns a plain-text financial snapshot for the LLM system prompt.
    Called once per chat request before the first LLM pass.

    Returns empty string if ALL fetches fail (so the LLM still responds,
    just without personalized context).
    """
    # Import here to avoid circular imports at module load time
    from services.analytics_service import (
        financial_health_score,
        emi_pressure,
        spending_patterns,
        net_worth_analysis_service,
        savings_optimization_analysis,
        cashflow_forecast,
        income_stability_analysis,
        burn_rate_analysis,
    )

    # ── Concurrent fetch ──────────────────────────────────────────────────────
    (
        health_result,
        emi_result,
        patterns_result,
        networth_result,
        savings_result,
        cashflow_result,
        stability_result,
        burn_result,
    ) = await asyncio.gather(
        _safe(financial_health_score(user_id),      "health_score",  None),
        _safe(emi_pressure(user_id),                "emi_pressure",  None),
        _safe(spending_patterns(user_id),           "spending",      []),
        _safe(net_worth_analysis_service(user_id),  "net_worth",     None),
        _safe(savings_optimization_analysis(user_id), "savings",     None),
        _safe(cashflow_forecast(user_id, [30, 90]), "cashflow",      []),
        _safe(income_stability_analysis(user_id),   "stability",     None),
        _safe(burn_rate_analysis(user_id),          "burn_rate",     None),
    )

    # ── Build compact context string ──────────────────────────────────────────
    lines: list[str] = ["=== USER FINANCIAL SNAPSHOT ==="]

    # Financial health
    if health_result:
        score, savings_rate, stability, burn_rate, risk = health_result
        lines.append(
            f"Financial Health: {score:.0f}/100 (risk: {risk})"
        )

    # Net worth
    if networth_result:
        assets, liabilities, net = networth_result
        lines.append(
            f"Net Worth: ₹{net:,.0f} (assets ₹{assets:,.0f}, liabilities ₹{liabilities:,.0f})"
        )

    # Savings
    if savings_result:
        rate, fin_score = savings_result
        lines.append(f"Savings Rate: {rate:.1f}%")

    # EMI pressure
    if emi_result:
        monthly_emi, emi_ratio, surv_score, risk_label = emi_result
        if monthly_emi > 0:
            lines.append(
                f"Monthly EMI Burden: ₹{monthly_emi:,.0f} "
                f"({emi_ratio*100:.1f}% of income, {risk_label})"
            )

    # Income stability
    if stability_result:
        volatility, predictability = stability_result
        lines.append(f"Income Predictability: {predictability:.0f}/100")

    # Burn rate
    if burn_result:
        burn_rate, days_left, days_elapsed = burn_result
        if burn_rate > 0:
            days_str = f"{days_left}d left" if days_left < 9999 else "no budget set"
            lines.append(f"Burn Rate: ₹{burn_rate:,.0f}/day ({days_str} in budget)")

    # Cashflow forecast
    if cashflow_result:
        for horizon, balance in cashflow_result:
            sign = "+" if balance >= 0 else ""
            lines.append(f"Cashflow ({horizon}d): {sign}₹{balance:,.0f}")

    # Top spending categories (max 5 to keep prompt tight)
    if patterns_result:
        top = patterns_result[:5]
        cat_str = ", ".join(f"{cat} {pct:.0f}%" for cat, pct in top)
        lines.append(f"Top Spending: {cat_str}")

    if len(lines) == 1:
        # All fetches failed — return empty so prompt stays clean
        return ""

    lines.append("=== END SNAPSHOT ===")
    return "\n".join(lines)


def format_context_for_prompt(context: str) -> str:
    """
    Wraps the context snapshot for injection into the system prompt.
    Returns empty string if context is empty (no data available).
    """
    if not context.strip():
        return ""
    return (
        "\n\nHere is the current user's real financial data. "
        "Use these numbers when answering — do not make up figures:\n"
        + context
        + "\n"
    )