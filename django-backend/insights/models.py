import strawberry
from typing import List, Optional
from enum import Enum


@strawberry.type
class AnalyticsPoint:
    period: str
    income: float
    expense: float
    total: float


@strawberry.type
class CategoryPoint:
    category: str
    total: float
    count: int


@strawberry.type
class AnalyticsMeta:
    truncated: bool
    limit_applied: bool
    row_count: int


@strawberry.enum
class WarningCode(Enum):
    LIMIT_EXCEEDED = "LIMIT_EXCEEDED"


@strawberry.type
class Warning:
    code: WarningCode
    message: str


@strawberry.type
class AnalyticsResult:
    data: List[AnalyticsPoint]
    meta: Optional[AnalyticsMeta] = None
    warnings: Optional[List[Warning]] = None


@strawberry.type
class CategoryResult:
    data: List[CategoryPoint]
    meta: Optional[AnalyticsMeta] = None
    warnings: Optional[List[Warning]] = None

@strawberry.type
class BudgetBreachResult:
    breach_probability: float
    expected_spend: float
    p95_days_to_breach: Optional[int]


@strawberry.type
class EmiPressureResult:
    monthly_emi: float
    emi_ratio: float
    survivability_score: float
    risk_level: str


@strawberry.type
class CashflowForecastPoint:
    horizon_days: int
    expected_balance: float


@strawberry.type
class CashflowForecastResult:
    points: List[CashflowForecastPoint]


@strawberry.type
class RecurringAnomaly:
    description: str
    severity: float
    deviation_percent: float
