from datetime import datetime
import pytz

UTC = pytz.UTC


def serialize_datetime(dt):
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=UTC)
    return dt.isoformat().replace("+00:00", "Z")


def parse_date(date_str):
    try:
        return datetime.fromisoformat(date_str)
    except:
        return None

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
