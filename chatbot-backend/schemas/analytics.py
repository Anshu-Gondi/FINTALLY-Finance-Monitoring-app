from pydantic import BaseModel
from typing import List, Optional
from enum import Enum


class TransactionAnomaly(BaseModel):
    datetime: str
    price: float
    zscore: float


class TransactionAnomalyResult(BaseModel):
    threshold: float
    anomalies: List[TransactionAnomaly]
    count: int


class CategoryDriftPoint(BaseModel):
    category: str
    percent_change: float


class CategoryDriftResult(BaseModel):
    category_drift: List[CategoryDriftPoint]


class RecurringImpactResult(BaseModel):
    monthly_recurring_cost: float
    yearly_projection: float


class BudgetUtilizationResult(BaseModel):
    budget_amount: float
    spent: float
    remaining: float
    usage_percent: float


class BurnRateResult(BaseModel):
    daily_burn_rate: float
    days_until_exhaustion: int
    days_elapsed: int

class AnalyticsPoint(BaseModel):
    period: str
    income: float
    expense: float
    total: float


class CategoryPoint(BaseModel):
    category: str
    total: float
    count: int


class AnalyticsMeta(BaseModel):
    truncated: bool
    limit_applied: bool
    row_count: int


class WarningCode(str, Enum):
    LIMIT_EXCEEDED = "LIMIT_EXCEEDED"


class Warning(BaseModel):
    code: WarningCode
    message: str


class AnalyticsResult(BaseModel):
    data: List[AnalyticsPoint]
    meta: Optional[AnalyticsMeta] = None
    warnings: Optional[List[Warning]] = None


class CategoryResult(BaseModel):
    data: List[CategoryPoint]
    meta: Optional[AnalyticsMeta] = None
    warnings: Optional[List[Warning]] = None


class BudgetBreachResult(BaseModel):
    breach_probability: float
    expected_spend: float
    p50_days_to_breach: Optional[int]


class EmiPressureResult(BaseModel):
    monthly_emi: float
    emi_ratio: float
    survivability_score: float
    risk_level: str


class CashflowForecastPoint(BaseModel):
    horizon_days: int
    expected_balance: float


class CashflowForecastResult(BaseModel):
    points: List[CashflowForecastPoint]


class RecurringAnomaly(BaseModel):
    description: str
    severity: float
    deviation_percent: float