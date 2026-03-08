from fastapi import APIRouter, Depends, Query
from typing import List, Optional

from dependencies.auth import get_current_user

from schemas.analytics import (
    AnalyticsPoint,
    AnalyticsResult,
    BudgetUtilizationResult,
    BurnRateResult,
    CategoryDriftPoint,
    CategoryDriftResult,
    CategoryPoint,
    CategoryResult,
    EmiPressureResult,
    CashflowForecastPoint,
    CashflowForecastResult,
    BudgetBreachResult,
    RecurringAnomaly,
    RecurringImpactResult,
    TransactionAnomaly,
    TransactionAnomalyResult,
)

from services.analytics_service import (
    daily_summary,
    period_summary,
    lifetime_analysis,
    category_summary,
    trend_summary,
    min_max_transaction,
    emi_pressure,
    cashflow_forecast,
    budget_breach_prediction,
    recurring_anomalies,
    anomalies,
    category_drift_analysis,
    recurring_impact_analysis,
    budget_utilization_analysis,
    burn_rate_analysis,
)

router = APIRouter()


# ---------------- DAILY SUMMARY ----------------
@router.get("/daily-summary", response_model=AnalyticsResult)
def get_daily_summary(
    interval: int = 1,
    user_id: str = Depends(get_current_user),
):

    result = daily_summary(user_id, interval)

    return AnalyticsResult(
        data=[AnalyticsPoint(period=p, income=i, expense=e, total=t)
              for p, i, e, t in result]
    )


# ---------------- PERIOD SUMMARY ----------------
@router.get("/period-summary", response_model=AnalyticsResult)
def get_period_summary(
    range: str = "weekly",
    bucket_days: Optional[int] = None,
    user_id: str = Depends(get_current_user),
):

    result = period_summary(user_id, range, bucket_days)

    return AnalyticsResult(
        data=[AnalyticsPoint(period=p, income=i, expense=e, total=t)
              for p, i, e, t in result]
    )


# ---------------- LIFETIME ----------------
@router.get("/lifetime-analysis", response_model=AnalyticsResult)
def get_lifetime_analysis(
    user_id: str = Depends(get_current_user),
):

    result = lifetime_analysis(user_id)

    return AnalyticsResult(
        data=[AnalyticsPoint(period=p, income=i, expense=e, total=t)
              for p, i, e, t in result]
    )


# ---------------- CATEGORY SUMMARY ----------------
@router.get("/category-summary", response_model=CategoryResult)
def get_category_summary(
    start: Optional[str] = None,
    end: Optional[str] = None,
    type: str = "all",
    keyword: Optional[str] = None,
    limit: Optional[int] = None,
    user_id: str = Depends(get_current_user),
):

    result = category_summary(user_id, start, end, type, keyword, limit)

    return CategoryResult(
        data=[CategoryPoint(category=c, total=t, count=n)
              for c, t, n in result]
    )


# ---------------- TREND SUMMARY ----------------
@router.get("/trend-summary", response_model=AnalyticsResult)
def get_trend_summary(
    range: str = "6months",
    user_id: str = Depends(get_current_user),
):

    result = trend_summary(user_id, range)

    return AnalyticsResult(
        data=[AnalyticsPoint(period=p, income=i, expense=e, total=t)
              for p, i, e, t in result]
    )


# ---------------- MIN MAX TRANSACTION ----------------
@router.get("/min-max-transaction", response_model=AnalyticsResult)
def get_min_max_transaction(
    user_id: str = Depends(get_current_user),
):

    min_result, max_result = min_max_transaction(user_id)

    data = []

    if max_result:
        d, v = max_result
        data.append(
            AnalyticsPoint(
                period=d,
                income=v if v > 0 else 0,
                expense=-v if v < 0 else 0,
                total=v,
            )
        )

    if min_result:
        d, v = min_result
        data.append(
            AnalyticsPoint(
                period=d,
                income=v if v > 0 else 0,
                expense=-v if v < 0 else 0,
                total=v,
            )
        )

    return AnalyticsResult(data=data)


# ---------------- EMI PRESSURE ----------------
@router.get("/emi-pressure", response_model=EmiPressureResult)
def get_emi_pressure(
    user_id: str = Depends(get_current_user),
):

    monthly_emi, score, label = emi_pressure(user_id)

    return EmiPressureResult(
        monthly_emi=monthly_emi,
        survivability_score=score,
        risk_level=label,
    )


# ---------------- CASHFLOW FORECAST ----------------
@router.get("/cashflow-forecast", response_model=CashflowForecastResult)
def get_cashflow_forecast(
    horizons: List[int] = Query([30, 60, 90]),
    user_id: str = Depends(get_current_user),
):

    result = cashflow_forecast(user_id, horizons)

    return CashflowForecastResult(
        points=[
            CashflowForecastPoint(
                horizon_days=h,
                expected_balance=b,
            )
            for h, b in result
        ]
    )


# ---------------- BUDGET BREACH ----------------
@router.get("/budget-breach", response_model=BudgetBreachResult)
def get_budget_breach(
    end_date: str,
    simulations: int = 2000,
    user_id: str = Depends(get_current_user),
):

    prob, expected, p50 = budget_breach_prediction(
        user_id, end_date, simulations
    )

    return BudgetBreachResult(
        breach_probability=prob,
        expected_spend=expected,
        p50_days_to_breach=p50,
    )


# ---------------- RECURRING ANOMALIES ----------------
@router.get("/recurring-anomalies", response_model=List[RecurringAnomaly])
def get_recurring_anomalies(
    user_id: str = Depends(get_current_user),
):

    result = recurring_anomalies(user_id)

    return [
        RecurringAnomaly(
            description=d,
            severity=s,
            deviation_percent=p,
        )
        for d, s, p in result
    ]
    
# ---------------- TRANSACTION ANOMALIES ----------------
@router.get("/anomalies", response_model=TransactionAnomalyResult)
def get_anomalies(
    threshold: float = 2.5,
    user_id: str = Depends(get_current_user),
):

    result = anomalies(user_id, threshold)

    return TransactionAnomalyResult(
        threshold=threshold,
        anomalies=[
            TransactionAnomaly(
                datetime=d,
                price=p,
                zscore=z
            )
            for d, p, z in result
        ],
        count=len(result)
    )

# ---------------- CATEGORY DRIFT ----------------
@router.get("/category-drift", response_model=CategoryDriftResult)
def get_category_drift(
    user_id: str = Depends(get_current_user),
):

    result = category_drift_analysis(user_id)

    return CategoryDriftResult(
        category_drift=[
            CategoryDriftPoint(
                category=c,
                percent_change=p
            )
            for c, p in result
        ]
    )

# ---------------- RECURRING IMPACT ----------------
@router.get("/recurring-impact", response_model=RecurringImpactResult)
def get_recurring_impact(
    user_id: str = Depends(get_current_user),
):

    monthly, yearly = recurring_impact_analysis(user_id)

    return RecurringImpactResult(
        monthly_recurring_cost=monthly,
        yearly_projection=yearly
    )
    
# ---------------- BUDGET UTILIZATION ----------------
@router.get("/budget-utilization", response_model=BudgetUtilizationResult)
def get_budget_utilization(
    user_id: str = Depends(get_current_user),
):

    spent, remaining, percent, amount = budget_utilization_analysis(user_id)

    return BudgetUtilizationResult(
        budget_amount=amount,
        spent=spent,
        remaining=remaining,
        usage_percent=percent
    )

# ---------------- BURN RATE ----------------
@router.get("/burn-rate", response_model=BurnRateResult)
def get_burn_rate(
    user_id: str = Depends(get_current_user),
):

    burn_rate, days_left, days_elapsed = burn_rate_analysis(user_id)

    return BurnRateResult(
        daily_burn_rate=burn_rate,
        days_until_exhaustion=days_left,
        days_elapsed=days_elapsed
    )