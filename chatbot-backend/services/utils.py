from datetime import datetime, timezone
from bson import ObjectId

UTC = timezone.utc


def serialize_datetime(dt: datetime) -> str:
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=UTC)
    return dt.isoformat().replace("+00:00", "Z")


def parse_date(date_str: str):
    try:
        return datetime.fromisoformat(date_str)
    except Exception:
        return None


def get_active_budget(budgets, user_id: str):
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