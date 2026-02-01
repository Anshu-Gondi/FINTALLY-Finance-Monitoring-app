import strawberry
from strawberry.types import Info

from datetime import datetime, timedelta
from dateutil.relativedelta import relativedelta
from typing import List, Optional
from bson import ObjectId
from pymongo import MongoClient
import os
import pytz

from rust_backend import (
    aggregate_by_interval,
    aggregate_by_category,
    aggregate_by_day,
    aggregate_by_month,
    aggregate_trend,
    find_min_max,

    # advanced analytics
    predict_budget_breach,          # Monte Carlo
    emi_survivability_score,
    emi_monthly_pressure,
    cashflow_forecast,
    detect_recurring_anomalies,
)

from .auth import get_current_user
from .models import (
    AnalyticsPoint,
    BudgetBreachResult,
    CashflowForecastPoint,
    CashflowForecastResult,
    CategoryPoint,
    AnalyticsMeta,
    AnalyticsResult,
    CategoryResult,
    EmiPressureResult,
    RecurringAnomaly,
    Warning,
    WarningCode,
)
from .utils import serialize_datetime, parse_date

# ------------------------------------------------------------------
# DB SETUP
# ------------------------------------------------------------------
UTC = pytz.UTC

client = MongoClient(os.getenv("MONGO_URL"))
db = client["test"]
transactions = db.transactions
budgets = db.budgets

def get_active_budget(user_id: str):
    now = datetime.utcnow().replace(tzinfo=UTC)

    return budgets.find_one(
        {
            "userId": ObjectId(user_id),
            "startDate": {"$lte": now},
            "$or": [
                {"endDate": None},
                {"endDate": {"$gte": now}},
            ],
        },
        sort=[("startDate", -1)],
    )


# ------------------------------------------------------------------
# QUERY ROOT
# ------------------------------------------------------------------


@strawberry.type
class Query:

    # -------------------- DAILY --------------------
    @strawberry.field
    def daily_summary(self, info: Info, interval: int = 1) -> AnalyticsResult:
        user_id = get_current_user(info)

        now = datetime.utcnow().replace(tzinfo=UTC)
        start = datetime(now.year, now.month, now.day, tzinfo=UTC)

        cursor = transactions.find(
            {"userId": ObjectId(user_id), "datetime": {
                "$gte": start, "$lte": now}},
            {"datetime": 1, "price": 1},
        )

        ts, prices = [], []
        for doc in cursor:
            ts.append(serialize_datetime(doc["datetime"]))
            prices.append(float(doc["price"]))

        result = aggregate_by_interval(ts, prices, interval)

        return AnalyticsResult(
            data=[AnalyticsPoint(period=p, income=i, expense=e, total=t)
                  for p, i, e, t in result]
        )

    # -------------------- PERIOD SUMMARY (weekly/monthly) --------------------
    @strawberry.field
    def period_summary(self, info: Info, range: str = "weekly", bucket_days: Optional[int] = None) -> AnalyticsResult:
        user_id = get_current_user(info)
        now = datetime.utcnow().replace(tzinfo=UTC)

        if range == "weekly":
            start = now - timedelta(days=6)
        elif range == "monthly":
            start = datetime(now.year, now.month, 1, tzinfo=UTC)
        else:
            raise ValueError("Invalid range")

        cursor = transactions.find(
            {"userId": ObjectId(user_id), "datetime": {
                "$gte": start, "$lte": now}},
            {"datetime": 1, "price": 1},
        )

        dates, prices = [], []
        for doc in cursor:
            dates.append(serialize_datetime(doc["datetime"]))
            prices.append(float(doc["price"]))

        result = aggregate_by_day(dates, prices, bucket_days)

        return AnalyticsResult(
            data=[AnalyticsPoint(period=p, income=i, expense=e, total=t)
                  for p, i, e, t in result]
        )

    # -------------------- LIFETIME --------------------
    @strawberry.field
    def lifetime_analysis(self, info: Info) -> AnalyticsResult:
        user_id = get_current_user(info)

        cursor = transactions.find({"userId": ObjectId(user_id)}, {
                                   "datetime": 1, "price": 1})

        dates, prices = [], []
        for doc in cursor:
            dates.append(serialize_datetime(doc["datetime"]))
            prices.append(float(doc["price"]))

        result = aggregate_by_month(dates, prices)

        return AnalyticsResult(
            data=[AnalyticsPoint(period=p, income=i, expense=e, total=t)
                  for p, i, e, t in result]
        )

    # -------------------- CATEGORY --------------------
    @strawberry.field
    def category_summary(
        self,
        info: Info,
        start: Optional[str] = None,
        end: Optional[str] = None,
        type: str = "all",
        keyword: Optional[str] = None,
        limit: Optional[int] = None,
    ) -> CategoryResult:
        user_id = get_current_user(info)
        match = {"userId": ObjectId(user_id)}

        if start and end:
            s, e = parse_date(start), parse_date(end)
            if s and e:
                match["datetime"] = {"$gte": s.replace(
                    tzinfo=UTC), "$lte": e.replace(tzinfo=UTC)}

        if keyword:
            match["description"] = {"$regex": keyword, "$options": "i"}

        if type == "income":
            match["price"] = {"$gt": 0}
        elif type == "expense":
            match["price"] = {"$lt": 0}

        cursor = transactions.find(match, {"category": 1, "price": 1})

        categories, prices = [], []
        for doc in cursor:
            categories.append(doc.get("category", "Uncategorized"))
            prices.append(float(doc["price"]))

        total_categories = len(set(categories))
        result = aggregate_by_category(categories, prices, limit)

        truncated = limit is not None and total_categories > limit

        return CategoryResult(
            data=[CategoryPoint(category=c, total=t, count=n)
                  for c, t, n in result],
            meta=AnalyticsMeta(
                truncated=truncated, limit_applied=limit is not None, row_count=total_categories),
            warnings=[Warning(code=WarningCode.LIMIT_EXCEEDED,
                              message=f"Top {limit} categories returned")] if truncated else None,
        )

    # -------------------- TRENDS --------------------
    @strawberry.field
    def trend_summary(self, info: Info, range: str = "6months") -> AnalyticsResult:
        user_id = get_current_user(info)
        now = datetime.utcnow().replace(tzinfo=UTC)

        if range == "6months":
            start, mode = now - relativedelta(months=6), "monthly"
        elif range == "12weeks":
            start, mode = now - timedelta(weeks=12), "weekly"
        else:
            raise ValueError("Invalid range")

        cursor = transactions.find(
            {"userId": ObjectId(user_id), "datetime": {
                "$gte": start, "$lte": now}},
            {"datetime": 1, "price": 1},
        )

        dates, prices = [], []
        for doc in cursor:
            dates.append(serialize_datetime(doc["datetime"]))
            prices.append(float(doc["price"]))

        result = aggregate_trend(dates, prices, mode)

        return AnalyticsResult(
            data=[AnalyticsPoint(period=p, income=i, expense=e, total=t)
                  for p, i, e, t in result]
        )

    # -------------------- MAX / MIN TRANSACTIONS --------------------
    @strawberry.field
    def min_max_transaction(self, info: Info) -> AnalyticsResult:
        """
        Returns the min and max transactions for the current user.
        """
        user_id = get_current_user(info)

        cursor = transactions.find(
            {"userId": ObjectId(user_id)},
            {"datetime": 1, "price": 1},
        )

        dates: list[str] = []
        prices: list[float] = []

        for doc in cursor:
            dates.append(serialize_datetime(doc["datetime"]))
            prices.append(float(doc["price"]))

        if not dates or not prices:
            return AnalyticsResult(data=[])

        # ✅ CORRECT CALL
        min_result, max_result = find_min_max(dates, prices)

        data = []

        if max_result is not None:
            max_date, max_val = max_result
            data.append(
                AnalyticsPoint(
                    period=max_date,
                    income=max_val if max_val > 0 else 0,
                    expense=-max_val if max_val < 0 else 0,
                    total=max_val,
                )
            )

        if min_result is not None:
            min_date, min_val = min_result
            data.append(
                AnalyticsPoint(
                    period=min_date,
                    income=min_val if min_val > 0 else 0,
                    expense=-min_val if min_val < 0 else 0,
                    total=min_val,
                )
            )

        return AnalyticsResult(data=data)

    # -------------------- EMI PRESSURE --------------------
    @strawberry.field
    def emi_pressure(self, info: Info) -> EmiPressureResult:
        user_id = get_current_user(info)

        cursor = transactions.find(
            {"userId": ObjectId(user_id), "isEmi": True},
            {"datetime": 1, "price": 1, "interestRate": 1, "tenureMonths": 1},
        )

        dates, principals, rates, tenures = [], [], [], []
        for d in cursor:
            dates.append(serialize_datetime(d["datetime"]))
            principals.append(abs(float(d["price"])))
            rates.append(float(d.get("interestRate", 12.0)))
            tenures.append(int(d.get("tenureMonths", 12)))

        monthly = emi_monthly_pressure(dates, principals, rates, tenures)
        total_emi = sum(v for _, v in monthly)

        income_cursor = transactions.find(
            {"userId": ObjectId(user_id), "price": {"$gt": 0}},
            {"price": 1},
        )
        monthly_income = sum(float(d["price"]) for d in income_cursor)

        score, label = emi_survivability_score(monthly_income, total_emi)

        return EmiPressureResult(
            monthly_emi=total_emi,
            survivability_score=score,
            risk_level=label,
        )

    # -------------------- CASHFLOW FORECAST --------------------
    @strawberry.field
    def cashflow_forecast(
        self,
        info: Info,
        horizons: List[int] = [30, 60, 90],
    ) -> CashflowForecastResult:
        user_id = get_current_user(info)

        cursor = transactions.find(
            {"userId": ObjectId(user_id)},
            {"datetime": 1, "price": 1},
        )

        dates, prices = [], []
        for d in cursor:
            dates.append(serialize_datetime(d["datetime"]))
            prices.append(float(d["price"]))

        forecast = cashflow_forecast(dates, prices, horizons)

        return CashflowForecastResult(
            points=[
                CashflowForecastPoint(
                    horizon_days=h,
                    expected_balance=b,
                )
                for h, b in forecast
            ]
        )

    # -------------------- BUDGET BREACH --------------------
    @strawberry.field
    def budget_breach_prediction(
        self,
        info: Info,
        end_date: str,
        simulations: int = 2_000,
    ) -> BudgetBreachResult:
        user_id = get_current_user(info)

        # 1️⃣ Load active budget
        budget = get_active_budget(user_id)
        if not budget:
            raise ValueError("No active budget found")

        budget_amount = float(budget["amount"])
        budget_start = budget["startDate"].replace(tzinfo=UTC)

        # 2️⃣ Parse end date
        end = parse_date(end_date)
        if not end:
            raise ValueError("Invalid end_date")

        end = end.replace(tzinfo=UTC)

        horizon_days = max((end - budget_start).days, 1)

        # 3️⃣ Fetch ONLY budget-period transactions
        cursor = transactions.find(
            {
                "userId": ObjectId(user_id),
                "datetime": {"$gte": budget_start, "$lte": end},
            },
            {"datetime": 1, "price": 1},
        )

        dates, prices = [], []
        for d in cursor:
            dates.append(serialize_datetime(d["datetime"]))
            prices.append(float(d["price"]))

        # 4️⃣ Call NEW Rust signature
        prob, expected, p50 = predict_budget_breach(
            dates=dates,
            prices=prices,
            budget_amount=budget_amount,
            horizon_days=horizon_days,
            simulations=simulations,
        )

        return BudgetBreachResult(
            breach_probability=prob,
            expected_spend=expected,
            p50_days_to_breach=p50,
        )

    # -------------------- RECURRING ANOMALIES --------------------
    @strawberry.field
    def recurring_anomalies(self, info: Info) -> List[RecurringAnomaly]:
        user_id = get_current_user(info)

        cursor = transactions.find(
            {"userId": ObjectId(user_id)},
            {"description": 1, "price": 1, "datetime": 1},
        )

        descs, prices, dates = [], [], []
        for d in cursor:
            descs.append(d.get("description", ""))
            prices.append(float(d["price"]))
            dates.append(serialize_datetime(d["datetime"]))

        anomalies = detect_recurring_anomalies(descs, prices, dates)

        return [
            RecurringAnomaly(
                description=desc,
                severity=sev,
                deviation_percent=dev,
            )
            for desc, sev, dev in anomalies
        ]