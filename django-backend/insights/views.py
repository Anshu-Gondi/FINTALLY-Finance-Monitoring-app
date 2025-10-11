from django.http import JsonResponse
from pymongo import ASCENDING, MongoClient, DESCENDING
from datetime import datetime, timedelta
from dateutil.relativedelta import relativedelta
import os
from dotenv import load_dotenv
from bson import ObjectId
from bson.json_util import dumps
import pytz
from rust_backend import aggregate_by_interval, aggregate_by_category, aggregate_by_day, aggregate_by_month, find_min_max, aggregate_trend

load_dotenv()
MONGO_URL = os.getenv("MONGO_URL")
client = MongoClient(MONGO_URL)
db = client["test"]
transactions = db.transactions

UTC = pytz.UTC  # helper for timezone-aware datetime


def parse_date(date_str):
    try:
        return datetime.fromisoformat(date_str)
    except:
        return None


def serialize_datetime(dt):
    """Convert datetime to ISO 8601 with Z for Rust parsing"""
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=UTC)
    return dt.isoformat().replace("+00:00", "Z")


def daily_summary(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "Missing user_id"}, status=400)

    interval = int(request.GET.get("interval", 1))
    now = datetime.utcnow().replace(tzinfo=UTC)
    start = datetime(now.year, now.month, now.day, tzinfo=UTC)

    cursor = transactions.find(
        {"userId": ObjectId(user_id), "datetime": {"$gte": start, "$lte": now}},
        {"datetime": 1, "price": 1}
    )

    timestamps, prices = [], []
    for doc in cursor:
        timestamps.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    result = aggregate_by_interval(timestamps, prices, interval)

    formatted = [
        {"period": period, "income": income, "expense": expense, "total": total}
        for period, income, expense, total in result
    ]

    return JsonResponse(formatted, safe=False)


def weekly_summary(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "Missing user_id"}, status=400)

    now = datetime.utcnow().replace(tzinfo=UTC)
    start = now - timedelta(days=6)
    start = datetime(start.year, start.month, start.day, tzinfo=UTC)

    bucket_days = request.GET.get("bucket")

    cursor = transactions.find(
        {"userId": ObjectId(user_id), "datetime": {"$gte": start, "$lte": now}},
        {"datetime": 1, "price": 1}
    )

    dates, prices = [], []
    for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    bucket_val = int(bucket_days) if bucket_days and bucket_days.isdigit() else None
    result = aggregate_by_day(dates, prices, bucket_val)

    formatted = [
        {"period": day, "income": inc, "expense": exp, "total": total}
        for day, inc, exp, total in result
    ]

    return JsonResponse(formatted, safe=False)


def monthly_summary(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "Missing user_id"}, status=400)

    now = datetime.utcnow().replace(tzinfo=UTC)
    start = datetime(now.year, now.month, 1, tzinfo=UTC)
    next_month = start + relativedelta(months=1)

    bucket_days = request.GET.get("bucket")

    cursor = transactions.find(
        {"userId": ObjectId(user_id), "datetime": {"$gte": start, "$lt": next_month}},
        {"datetime": 1, "price": 1},
    )

    dates, prices = [], []
    for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    bucket_val = int(bucket_days) if bucket_days and bucket_days.isdigit() else None
    result = aggregate_by_day(dates, prices, bucket_val)

    formatted = [
        {"period": day, "income": inc, "expense": exp, "total": total}
        for day, inc, exp, total in result
    ]

    return JsonResponse(formatted, safe=False)


def lifetime_analysis(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "user_id required"}, status=400)

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"datetime": 1, "price": 1}
    )

    dates, prices = [], []
    for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    result = aggregate_by_month(dates, prices)

    formatted = [
        {"period": period, "income": inc, "expense": exp, "total": total}
        for period, inc, exp, total in result
    ]

    return JsonResponse(formatted, safe=False)

# Category_summary
def category_summary(request):
    user_id = request.GET.get("user_id")
    start = request.GET.get("start")
    end = request.GET.get("end")
    type_filter = request.GET.get("type", "all")
    keyword = request.GET.get("keyword", "")
    limit = request.GET.get("limit")  # optional limit

    if not user_id:
        return JsonResponse({"error": "Missing user_id"}, status=400)

    match = {"userId": ObjectId(user_id)}

    if start and end:
        start_dt = parse_date(start)
        end_dt = parse_date(end)
        if start_dt and end_dt:
            match["datetime"] = {"$gte": start_dt.replace(tzinfo=UTC), "$lte": end_dt.replace(tzinfo=UTC)}

    if keyword:
        match["description"] = {"$regex": keyword, "$options": "i"}

    if type_filter == "income":
        match["price"] = {"$gt": 0}
    elif type_filter == "expense":
        match["price"] = {"$lt": 0}

    cursor = transactions.find(match, {"category": 1, "price": 1})

    categories, prices = [], []
    for doc in cursor:
        categories.append(doc.get("category", "Uncategorized"))
        prices.append(float(doc.get("price", 0)))

    limit_val = int(limit) if limit and limit.isdigit() else None
    result = aggregate_by_category(categories, prices, limit_val)

    formatted = [
        {"category": cat, "total": total, "count": count}
        for cat, total, count in result
    ]

    return JsonResponse({"data": formatted}, safe=False)


# max_transaction
def max_transaction(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "user_id required"}, status=400)

    cursor = transactions.find({"userId": ObjectId(user_id)}, {"datetime": 1, "price": 1})

    dates, prices = [], []
    for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    min_txn, max_txn = find_min_max(dates, prices)
    if not max_txn:
        return JsonResponse([], safe=False)

    dt, price = max_txn
    txn_type = "income" if price > 0 else "expense"
    abs_price = abs(price)
    formatted = {
        "period": dt.split("T")[0],
        "income": price if txn_type == "income" else 0,
        "expense": abs_price if txn_type == "expense" else 0,
        "total": abs_price
    }

    return JsonResponse([formatted], safe=False)


# min_transaction
def min_transaction(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "user_id required"}, status=400)

    cursor = transactions.find({"userId": ObjectId(user_id)}, {"datetime": 1, "price": 1})

    dates, prices = [], []
    for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    min_txn, _ = find_min_max(dates, prices)
    if not min_txn:
        return JsonResponse([], safe=False)

    dt, price = min_txn
    txn_type = "income" if price > 0 else "expense"
    abs_price = abs(price)
    formatted = {
        "period": dt.split("T")[0],
        "income": price if txn_type == "income" else 0,
        "expense": abs_price if txn_type == "expense" else 0,
        "total": abs_price
    }

    return JsonResponse([formatted], safe=False)

def trend_summary(request):
    user_id = request.GET.get("user_id")
    range_mode = request.GET.get("range", "6months")

    if not user_id:
        return JsonResponse({"error": "Missing user_id"}, status=400)

    now = datetime.utcnow().replace(tzinfo=UTC)

    if range_mode == "6months":
        start_date = now - relativedelta(months=6)
        mode = "monthly"
    elif range_mode == "12weeks":
        start_date = now - timedelta(weeks=12)
        mode = "weekly"
    else:
        return JsonResponse({"error": "Invalid range value"}, status=400)

    cursor = transactions.find(
        {"userId": ObjectId(user_id), "datetime": {"$gte": start_date, "$lte": now}},
        {"datetime": 1, "price": 1}
    )

    dates, prices = [], []
    for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    result = aggregate_trend(dates, prices, mode)

    formatted = [
        {"period": period, "income": inc, "expense": exp, "total": total}
        for period, inc, exp, total in result
    ]

    return JsonResponse({"data": formatted}, safe=False)