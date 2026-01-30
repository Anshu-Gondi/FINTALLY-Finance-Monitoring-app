import strawberry
from strawberry.types import Info

from datetime import datetime, timedelta
from dateutil.relativedelta import relativedelta
from typing import Optional
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
)

from .auth import get_current_user
from .models import (
    AnalyticsPoint,
    CategoryPoint,
    AnalyticsMeta,
    AnalyticsResult,
    CategoryResult,
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
            {"userId": ObjectId(user_id), "datetime": {"$gte": start, "$lte": now}},
            {"datetime": 1, "price": 1},
        )

        ts, prices = [], []
        for doc in cursor:
            ts.append(serialize_datetime(doc["datetime"]))
            prices.append(float(doc["price"]))

        result = aggregate_by_interval(ts, prices, interval)

        return AnalyticsResult(
            data=[AnalyticsPoint(period=p, income=i, expense=e, total=t) for p, i, e, t in result]
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
            {"userId": ObjectId(user_id), "datetime": {"$gte": start, "$lte": now}},
            {"datetime": 1, "price": 1},
        )

        dates, prices = [], []
        for doc in cursor:
            dates.append(serialize_datetime(doc["datetime"]))
            prices.append(float(doc["price"]))

        result = aggregate_by_day(dates, prices, bucket_days)

        return AnalyticsResult(
            data=[AnalyticsPoint(period=p, income=i, expense=e, total=t) for p, i, e, t in result]
        )

    # -------------------- LIFETIME --------------------
    @strawberry.field
    def lifetime_analysis(self, info: Info) -> AnalyticsResult:
        user_id = get_current_user(info)

        cursor = transactions.find({"userId": ObjectId(user_id)}, {"datetime": 1, "price": 1})

        dates, prices = [], []
        for doc in cursor:
            dates.append(serialize_datetime(doc["datetime"]))
            prices.append(float(doc["price"]))

        result = aggregate_by_month(dates, prices)

        return AnalyticsResult(
            data=[AnalyticsPoint(period=p, income=i, expense=e, total=t) for p, i, e, t in result]
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
                match["datetime"] = {"$gte": s.replace(tzinfo=UTC), "$lte": e.replace(tzinfo=UTC)}

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
            data=[CategoryPoint(category=c, total=t, count=n) for c, t, n in result],
            meta=AnalyticsMeta(truncated=truncated, limit_applied=limit is not None, row_count=total_categories),
            warnings=[Warning(code=WarningCode.LIMIT_EXCEEDED, message=f"Top {limit} categories returned")] if truncated else None,
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
            {"userId": ObjectId(user_id), "datetime": {"$gte": start, "$lte": now}},
            {"datetime": 1, "price": 1},
        )

        dates, prices = [], []
        for doc in cursor:
            dates.append(serialize_datetime(doc["datetime"]))
            prices.append(float(doc["price"]))

        result = aggregate_trend(dates, prices, mode)

        return AnalyticsResult(
            data=[AnalyticsPoint(period=p, income=i, expense=e, total=t) for p, i, e, t in result]
        )

    # -------------------- MAX / MIN TRANSACTIONS --------------------
    @strawberry.field
    def min_max_transaction(self, info: Info) -> AnalyticsResult:
        """
        Returns the single transaction with min and max price for the current user.
        """
        user_id = get_current_user(info)

        cursor = transactions.find({"userId": ObjectId(user_id)}, {"datetime": 1, "price": 1})
        dates, prices = [], []
        for doc in cursor:
            dates.append(serialize_datetime(doc["datetime"]))
            prices.append(float(doc["price"]))

        max_val, min_val = find_min_max(prices)

        # Find indices of min/max to get their dates
        max_idx = prices.index(max_val) if max_val is not None else None
        min_idx = prices.index(min_val) if min_val is not None else None

        data = []
        if max_idx is not None:
            data.append(AnalyticsPoint(period=dates[max_idx], income=max_val if max_val > 0 else 0, expense=-max_val if max_val < 0 else 0, total=max_val))
        if min_idx is not None and min_idx != max_idx:
            data.append(AnalyticsPoint(period=dates[min_idx], income=min_val if min_val > 0 else 0, expense=-min_val if min_val < 0 else 0, total=min_val))

        return AnalyticsResult(data=data)
