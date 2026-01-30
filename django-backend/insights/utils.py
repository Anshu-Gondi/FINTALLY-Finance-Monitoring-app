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
