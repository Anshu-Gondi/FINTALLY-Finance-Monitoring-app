from datetime import datetime, timezone
from bson import ObjectId
from services.db import budgets

UTC = timezone.utc


def serialize_datetime(dt: datetime) -> str:
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=UTC)
    return dt.isoformat().replace("+00:00", "Z")


def parse_date(date_str: str):
    if not date_str:
        return None
    try:
        return datetime.fromisoformat(date_str.replace("Z", "+00:00"))
    except Exception:
        return None


async def get_active_budget(user_id: str):
    now = datetime.utcnow().replace(tzinfo=UTC)
    return await budgets.find_one(
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