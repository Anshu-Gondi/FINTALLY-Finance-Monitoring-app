from django.http import JsonResponse
from pymongo import ASCENDING, MongoClient, DESCENDING
from datetime import datetime, timedelta
from dateutil.relativedelta import relativedelta
import os
from dotenv import load_dotenv
from bson import ObjectId
from bson.json_util import dumps

load_dotenv()
MONGO_URL = os.getenv("MONGO_URL")
client = MongoClient(MONGO_URL)
db = client["test"]
transactions = db.transactions


def parse_date(date_str):
    try:
        return datetime.fromisoformat(date_str)
    except:
        return None


def monthly_summary(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "Missing user_id"}, status=400)

    now = datetime.utcnow()
    start = datetime(now.year, now.month, 1)
    next_month = start + relativedelta(months=1)

    pipeline = [
        {
            "$match": {
                "userId": ObjectId(user_id),
                "datetime": {"$gte": start, "$lt": next_month}
            }
        },
        {
            "$group": {
                "_id": {
                    "year": {"$year": "$datetime"},
                    "month": {"$month": "$datetime"},
                    "day": {"$dayOfMonth": "$datetime"}
                },
                "income": {
                    "$sum": { "$cond": [ {"$gt": ["$price", 0]}, "$price", 0 ] }
                },
                "expense": {
                    "$sum": { "$cond": [ {"$lt": ["$price", 0]}, "$price", 0 ] }
                }
            }
        },
        {"$sort": {"_id.year": 1, "_id.month": 1, "_id.day": 1}}
    ]

    result = list(transactions.aggregate(pipeline))
    formatted = [
        {
            "period": f"{r['_id']['year']}-{r['_id']['month']:02}-{r['_id']['day']:02}",
            "income": r["income"],
            "expense": abs(r["expense"]),
            "total": r["income"] + abs(r["expense"])
        } for r in result
    ]

    return JsonResponse(formatted, safe=False)


def weekly_summary(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "Missing user_id"}, status=400)

    now = datetime.utcnow()
    start = now - timedelta(days=6)
    start = datetime(start.year, start.month, start.day)

    pipeline = [
        {
            "$match": {
                "userId": ObjectId(user_id),
                "datetime": {"$gte": start, "$lte": now}
            }
        },
        {
            "$group": {
                "_id": {
                    "year": {"$year": "$datetime"},
                    "month": {"$month": "$datetime"},
                    "day": {"$dayOfMonth": "$datetime"}
                },
                "income": {
                    "$sum": { "$cond": [ {"$gt": ["$price", 0]}, "$price", 0 ] }
                },
                "expense": {
                    "$sum": { "$cond": [ {"$lt": ["$price", 0]}, "$price", 0 ] }
                }
            }
        },
        {"$sort": {"_id.year": 1, "_id.month": 1, "_id.day": 1}}
    ]

    result = list(transactions.aggregate(pipeline))
    formatted = [
        {
            "period": f"{r['_id']['year']}-{r['_id']['month']:02}-{r['_id']['day']:02}",
            "income": r["income"],
            "expense": abs(r["expense"]),
            "total": r["income"] + abs(r["expense"])
        } for r in result
    ]

    return JsonResponse(formatted, safe=False)


def daily_summary(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "Missing user_id"}, status=400)

    now = datetime.utcnow()
    start = datetime(now.year, now.month, now.day)

    pipeline = [
        {
            "$match": {
                "userId": ObjectId(user_id),
                "datetime": {"$gte": start, "$lte": now}
            }
        },
        {
            "$group": {
                "_id": {
                    "hour": {"$hour": "$datetime"}
                },
                "income": {
                    "$sum": { "$cond": [ {"$gt": ["$price", 0]}, "$price", 0 ] }
                },
                "expense": {
                    "$sum": { "$cond": [ {"$lt": ["$price", 0]}, "$price", 0 ] }
                }
            }
        },
        {"$sort": {"_id.hour": 1}}
    ]

    result = list(transactions.aggregate(pipeline))
    formatted = [
        {
            "period": f"{r['_id']['hour']:02}:00",
            "income": r["income"],
            "expense": abs(r["expense"]),
            "total": r["income"] + abs(r["expense"])
        } for r in result
    ]

    return JsonResponse(formatted, safe=False)


def category_summary(request):
    user_id = request.GET.get("user_id")
    start = request.GET.get("start")
    end = request.GET.get("end")
    type_filter = request.GET.get("type", "all")
    keyword = request.GET.get("keyword", "")

    if not user_id:
        return JsonResponse({"error": "Missing user_id"}, status=400)

    match = {
        "userId": ObjectId(user_id)
    }

    if start and end:
        match["datetime"] = {
            "$gte": datetime.fromisoformat(start),
            "$lte": datetime.fromisoformat(end),
        }

    if keyword:
        match["description"] = {"$regex": keyword, "$options": "i"}

    # Filtering based on type (income/expense)
    if type_filter == "income":
        match["price"] = { "$gt": 0 }
    elif type_filter == "expense":
        match["price"] = { "$lt": 0 }

    pipeline = [
        {"$match": match},
        {
            "$group": {
                "_id": "$category",
                "total": { "$sum": { "$abs": "$price" } },
                "count": { "$sum": 1 }
            }
        },
        {
            "$sort": { "total": -1 }
        }
    ]

    results = list(transactions.aggregate(pipeline))

    return JsonResponse({ "data": results }, safe=False)


def lifetime_analysis(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "user_id required"}, status=400)

    pipeline = [
        {
            "$match": {
                "userId": ObjectId(user_id)
            }
        },
        {
            "$group": {
                "_id": {
                    "year": {"$year": "$datetime"},
                    "month": {"$month": "$datetime"}
                },
                "income": {
                    "$sum": {
                        "$cond": [{"$gt": ["$price", 0]}, "$price", 0]
                    }
                },
                "expense": {
                    "$sum": {
                        "$cond": [{"$lt": ["$price", 0]}, "$price", 0]
                    }
                }
            }
        },
        {"$sort": {"_id.year": 1, "_id.month": 1}}
    ]

    result = list(transactions.aggregate(pipeline))

    formatted_result = [
        {
            "period": f"{r['_id']['year']}-{r['_id']['month']:02}",
            "income": r["income"],
            "expense": abs(r["expense"]),
            "total": r["income"] + abs(r["expense"])
        } for r in result
    ]

    return JsonResponse(formatted_result, safe=False)


def max_transaction(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "user_id required"}, status=400)

    max_txn = transactions.find_one(
        {"userId": ObjectId(user_id)},
        sort=[("price", DESCENDING)]
    )

    if not max_txn:
        return JsonResponse([], safe=False)

    txn_type = "income" if max_txn["price"] > 0 else "expense"
    abs_price = abs(max_txn["price"])
    formatted = {
        "period": max_txn.get("datetime", datetime.utcnow()).strftime("%Y-%m-%d"),
        "income": max_txn["price"] if txn_type == "income" else 0,
        "expense": abs_price if txn_type == "expense" else 0,
        "total": abs_price
    }

    return JsonResponse([formatted], safe=False)

def min_transaction(request):
    user_id = request.GET.get("user_id")
    if not user_id:
        return JsonResponse({"error": "user_id required"}, status=400)

    min_txn = transactions.find_one(
        {"userId": ObjectId(user_id)},
        sort=[("price", ASCENDING)]
    )

    if not min_txn:
        return JsonResponse([], safe=False)

    txn_type = "income" if min_txn["price"] > 0 else "expense"
    abs_price = abs(min_txn["price"])
    formatted = {
        "period": min_txn.get("datetime", datetime.utcnow()).strftime("%Y-%m-%d"),
        "income": min_txn["price"] if txn_type == "income" else 0,
        "expense": abs_price if txn_type == "expense" else 0,
        "total": abs_price
    }

    return JsonResponse([formatted], safe=False)

def trend_summary(request):
    user_id = request.GET.get("user_id")
    range_mode = request.GET.get("range", "6months")

    if not user_id:
        return JsonResponse({"error": "Missing user_id"}, status=400)

    now = datetime.utcnow()

    if range_mode == "6months":
        start_date = now - relativedelta(months=6)
        group_format = {
            "year": {"$year": "$datetime"},
            "month": {"$month": "$datetime"}
        }
        format_label = lambda r: f"{r['_id']['year']}-{r['_id']['month']:02}"

    elif range_mode == "12weeks":
        start_date = now - timedelta(weeks=12)
        group_format = {
            "year": {"$year": "$datetime"},
            "week": {"$isoWeek": "$datetime"}
        }
        format_label = lambda r: f"{r['_id']['year']}-W{r['_id']['week']:02}"

    else:
        return JsonResponse({"error": "Invalid range value"}, status=400)

    pipeline = [
        {
            "$match": {
                "userId": ObjectId(user_id),
                "datetime": {"$gte": start_date, "$lte": now}
            }
        },
        {
            "$group": {
                "_id": group_format,
                "income": {
                    "$sum": {
                        "$cond": [{"$gt": ["$price", 0]}, "$price", 0]
                    }
                },
                "expense": {
                    "$sum": {
                        "$cond": [{"$lt": ["$price", 0]}, "$price", 0]
                    }
                }
            }
        },
        {"$sort": {"_id": 1}}
    ]

    result = list(transactions.aggregate(pipeline))

    formatted = [
        {
            "period": format_label(r),
            "income": r["income"],
            "expense": abs(r["expense"]),
            "total": r["income"] + abs(r["expense"])
        } for r in result
    ]

    return JsonResponse({ "data": formatted }, safe=False)
