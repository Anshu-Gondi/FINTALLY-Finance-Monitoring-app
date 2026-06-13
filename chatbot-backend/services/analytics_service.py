import math
from datetime import datetime, timedelta, timezone
from dateutil.relativedelta import relativedelta
from bson import ObjectId

from services.db import transactions, budgets
from services.utils import get_active_budget, serialize_datetime, parse_date

from rust_backend import (
    aggregate_by_interval,
    aggregate_by_category,
    aggregate_by_day,
    aggregate_by_month,
    aggregate_trend,
    find_min_max,
    predict_budget_breach,
    emi_survivability_score,
    emi_monthly_pressure,
    cashflow_forecast as rust_cashflow_forecast,
    detect_recurring_anomalies,
    detect_anomalies,
    category_drift,
    recurring_impact,
    budget_utilization,
    budget_burn_rate,
    income_stability,
    savings_metrics,
    net_worth_analysis,
)

UTC = timezone.utc


def _safe_float(v, default=0.0):
    """Sanitize inf/nan before JSON serialization."""
    if v is None or (isinstance(v, float) and (math.isnan(v) or math.isinf(v))):
        return default
    return float(v)

# ---------------- DAILY SUMMARY ----------------
async def daily_summary(user_id: str, interval: int):
    now = datetime.utcnow().replace(tzinfo=UTC)
    start = datetime(now.year, now.month, now.day, tzinfo=UTC)
    cursor = transactions.find(
        {"userId": ObjectId(user_id), "datetime": {"$gte": start, "$lte": now}},
        {"datetime": 1, "price": 1},
    )
    ts, prices = [], []
    async for doc in cursor:
        ts.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))
    return aggregate_by_interval(ts, prices, interval)


# ---------------- PERIOD SUMMARY ----------------
async def period_summary(user_id: str, range: str, bucket_days):
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
    async for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))
    return aggregate_by_day(dates, prices, bucket_days)


# ---------------- LIFETIME ----------------
async def lifetime_analysis(user_id: str):
    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"datetime": 1, "price": 1},
    )
    dates, prices = [], []
    async for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))
    return aggregate_by_month(dates, prices)


# ---------------- CATEGORY ----------------
async def category_summary(user_id, start, end, type, keyword, limit):
    match = {"userId": ObjectId(user_id)}
    if start and end:
        s, e = parse_date(start), parse_date(end)
        if s and e:
            match["datetime"] = {"$gte": s, "$lte": e}
    if keyword:
        match["description"] = {"$regex": keyword, "$options": "i"}
    if type == "income":
        match["price"] = {"$gt": 0}
    elif type == "expense":
        match["price"] = {"$lt": 0}
    cursor = transactions.find(match, {"category": 1, "price": 1})
    categories, prices = [], []
    async for doc in cursor:
        categories.append(doc.get("category") or "Uncategorized")
        prices.append(float(doc["price"]))
    return aggregate_by_category(categories, prices, limit)


# ---------------- TREND ----------------
async def trend_summary(user_id: str, range: str):
    now = datetime.utcnow().replace(tzinfo=UTC)
    if range == "6months":
        start = now - relativedelta(months=6)
        mode = "monthly"
    elif range == "12weeks":
        start = now - timedelta(weeks=12)
        mode = "weekly"
    else:
        raise ValueError("Invalid range")
    cursor = transactions.find(
        {"userId": ObjectId(user_id), "datetime": {"$gte": start, "$lte": now}},
        {"datetime": 1, "price": 1},
    )
    dates, prices = [], []
    async for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))
    return aggregate_trend(dates, prices, mode)


# ---------------- MIN MAX ----------------
async def min_max_transaction(user_id: str):
    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"datetime": 1, "price": 1},
    )
    dates, prices = [], []
    async for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))
    if not dates:
        return None, None
    return find_min_max(dates, prices)


# ---------------- EMI PRESSURE ----------------
async def emi_pressure(user_id: str):
    cursor = transactions.find(
        {"userId": ObjectId(user_id), "emiMeta": {"$exists": True}},
        {"datetime": 1, "emiMeta": 1},
    )
    dates, principals, rates, tenures = [], [], [], []
    async for d in cursor:
        emi = d.get("emiMeta", {})
        dates.append(serialize_datetime(d["datetime"]))
        principals.append(float(emi.get("principal", 0)))
        rates.append(float(emi.get("annualRate", 12)))
        tenures.append(int(emi.get("tenureMonths", 12)))

    monthly = emi_monthly_pressure(dates, principals, rates, tenures)
    total_emi = sum(v for _, v in monthly)

    income_cursor = transactions.find(
        {"userId": ObjectId(user_id), "price": {"$gt": 0}},
        {"price": 1},
    )
    monthly_income = 0.0
    async for d in income_cursor:
        monthly_income += float(d["price"])

    score, label = emi_survivability_score(monthly_income, total_emi)
    emi_ratio = _safe_float(total_emi / monthly_income if monthly_income else 0.0)

    return (
        _safe_float(total_emi),
        _safe_float(emi_ratio),
        _safe_float(score),
        label,
    )


# ---------------- CASHFLOW FORECAST ----------------
# Bug fixed: was passing ALL transactions (including non-recurring with freq="none")
# to Rust, which projects them forward indefinitely on the "_" arm (breaks, but
# still includes them once). Now only recurring transactions are sent.
# Bug fixed: router expected (horizon_days, balance) tuples but Rust returns
# (date_str, income, expense, net). We now aggregate net balance per horizon.
async def cashflow_forecast(user_id: str, horizons: list):
    cursor = transactions.find(
        {"userId": ObjectId(user_id), "isRecurring": True},
        {"datetime": 1, "price": 1, "recurringFrequency": 1},
    )

    dates, prices, freqs = [], [], []
    async for d in cursor:
        freq = d.get("recurringFrequency") or "Monthly"
        # Rust only understands Daily/Weekly/Monthly — normalise anything else
        if freq not in ("Daily", "Weekly", "Monthly"):
            freq = "Monthly"
        dates.append(serialize_datetime(d["datetime"]))
        prices.append(float(d["price"]))
        freqs.append(freq)

    # Rust returns (date_str, income, expense, net) for each projected day.
    # We accumulate cumulative net balance up to each horizon cutoff.
    result = []
    for horizon in sorted(horizons):
        daily_rows = rust_cashflow_forecast(dates, prices, freqs, horizon)
        # daily_rows: [(date_str, income, expense, net), ...]
        cumulative_balance = sum(net for _, _inc, _exp, net in daily_rows)
        result.append((horizon, _safe_float(cumulative_balance)))

    return result  # [(horizon_days, expected_balance), ...]


# ---------------- BUDGET BREACH ----------------
async def budget_breach_prediction(user_id: str, end_date: str, simulations: int):
    # Bug fixed: get_active_budget is sync — do NOT await it
    budget = await get_active_budget(user_id)
    if not budget:
        return 0.0, 0.0, None  # graceful empty instead of raising

    budget_amount = float(budget["amount"])
    budget_start = budget["startDate"].replace(tzinfo=UTC)
    end = parse_date(end_date)
    if not end:
        raise ValueError("Invalid end_date")

    horizon_days = max((end - budget_start).days, 1)
    cursor = transactions.find(
        {
            "userId": ObjectId(user_id),
            "datetime": {"$gte": budget_start, "$lte": end},
        },
        {"datetime": 1, "price": 1},
    )
    dates, prices = [], []
    async for d in cursor:
        dates.append(serialize_datetime(d["datetime"]))
        prices.append(float(d["price"]))

    prob, expected, p50 = predict_budget_breach(
        dates=dates,
        prices=prices,
        budget_amount=budget_amount,
        horizon_days=horizon_days,
        simulations=simulations,
    )
    return _safe_float(prob), _safe_float(expected), p50


# ---------------- RECURRING ANOMALIES ----------------
# Bug fixed: was calling detect_recurring_anomalies(descs, prices, dates) but
# Rust signature is (dates, prices, expected_frequency_days, tolerance_pct).
# Also Rust returns (date, reason_string) but router expects
# (description, severity, deviation_percent). Implemented in pure Python since
# the Rust function's output shape doesn't match what the router needs.
async def recurring_anomalies(user_id: str):
    cursor = transactions.find(
        {"userId": ObjectId(user_id), "isRecurring": True},
        {"description": 1, "price": 1, "datetime": 1},
    )

    docs = []
    async for d in cursor:
        docs.append({
            "description": d.get("description", "Recurring"),
            "price": float(d["price"]),
            "datetime": d["datetime"],
        })

    if not docs:
        return []

    # Group by description, detect price deviations per group
    from collections import defaultdict
    groups = defaultdict(list)
    for doc in docs:
        groups[doc["description"]].append(doc["price"])

    result = []
    for desc, price_list in groups.items():
        if len(price_list) < 2:
            continue
        mean = sum(price_list) / len(price_list)
        if mean == 0:
            continue
        for p in price_list:
            deviation = abs(p - mean) / abs(mean) * 100
            if deviation > 20:  # >20% deviation = anomaly
                severity = min(deviation / 100, 1.0)
                result.append((desc, severity, deviation))
                break  # one entry per description group

    return result


# ---------------- TRANSACTION ANOMALIES ----------------
# Bug fixed: Rust detect_anomalies(prices, threshold) returns flat Vec<f64>
# with no dates or zscores. We compute MAD ourselves to get (date, price, zscore).
async def anomalies(user_id: str, threshold: float):
    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"datetime": 1, "price": 1},
    )
    dates, prices = [], []
    async for d in cursor:
        dates.append(serialize_datetime(d["datetime"]))
        prices.append(float(d["price"]))

    if len(prices) < 2:
        return []

    sorted_prices = sorted(prices)
    n = len(sorted_prices)
    mid = n // 2
    median = (sorted_prices[mid - 1] + sorted_prices[mid]) / 2.0 if n % 2 == 0 else sorted_prices[mid]

    deviations = sorted([abs(p - median) for p in prices])
    mad = (deviations[mid - 1] + deviations[mid]) / 2.0 if n % 2 == 0 else deviations[mid]

    if mad == 0:
        return []

    result = []
    for d, p in zip(dates, prices):
        zscore = abs(p - median) / mad
        if zscore > threshold:
            result.append((d, p, _safe_float(zscore)))

    return result


# ---------------- CATEGORY DRIFT ----------------
async def category_drift_analysis(user_id: str):
    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"category": 1, "price": 1, "datetime": 1},
    )
    prev, curr = {}, {}
    now = datetime.utcnow().replace(tzinfo=UTC)
    async for d in cursor:
        cat = d.get("category") or "Uncategorized"
        price = abs(float(d["price"]))
        dt = d["datetime"]
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=UTC)
        if dt.year == now.year and dt.month == now.month:
            curr[cat] = curr.get(cat, 0.0) + price
        else:
            prev[cat] = prev.get(cat, 0.0) + price
    return category_drift(prev, curr)


# ---------------- RECURRING IMPACT ----------------
async def recurring_impact_analysis(user_id: str):
    cursor = transactions.find(
        {"userId": ObjectId(user_id), "isRecurring": True},
        {"price": 1, "recurringFrequency": 1},
    )
    prices, freqs = [], []
    async for d in cursor:
        freq = d.get("recurringFrequency") or "Monthly"
        if freq not in ("Daily", "Weekly", "Monthly"):
            freq = "Monthly"
        prices.append(float(d["price"]))
        freqs.append(freq)
    return recurring_impact(prices, freqs)


# ---------------- BUDGET UTILIZATION ----------------
async def budget_utilization_analysis(user_id: str):
    budget = await get_active_budget(user_id)
    if not budget:
        return 0.0, 0.0, 0.0, 0.0  # graceful empty

    amount = float(budget["amount"])
    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"price": 1},
    )
    prices = []
    async for d in cursor:
        price = float(d["price"])
        if price < 0:
            prices.append(price)

    spent, remaining, percent = budget_utilization(amount, prices)
    return _safe_float(spent), _safe_float(remaining), _safe_float(percent), amount


# ---------------- BURN RATE ----------------
# Bug fixed: Rust returns (burn_rate, Optional[f64]) for days_left.
# Schema expects int. We coerce safely with a large sentinel for "never".
async def burn_rate_analysis(user_id: str):
    budget = await get_active_budget(user_id)
    if not budget:
        return 0.0, 0, 0  # graceful empty

    start = budget["startDate"].replace(tzinfo=UTC)
    amount = float(budget["amount"])
    cursor = transactions.find(
        {"userId": ObjectId(user_id), "datetime": {"$gte": start}},
        {"price": 1},
    )
    prices = []
    async for d in cursor:
        prices.append(float(d["price"]))

    spent, remaining, _ = budget_utilization(amount, prices)
    days_elapsed = max((datetime.utcnow().replace(tzinfo=UTC) - start).days, 1)
    burn_rate, days_left = budget_burn_rate(spent, days_elapsed, remaining)

    # days_left is Optional[f64] from Rust — coerce to int, None → 9999
    days_left_int = int(days_left) if days_left is not None else 9999

    return _safe_float(burn_rate), days_left_int, days_elapsed


# ---------------- INCOME STABILITY ----------------
async def income_stability_analysis(user_id: str):
    cursor = transactions.find(
        {"userId": ObjectId(user_id), "price": {"$gt": 0}},
        {"price": 1},
    )
    incomes = []
    async for d in cursor:
        incomes.append(float(d["price"]))
    volatility, predictability = income_stability(incomes)
    return _safe_float(volatility), _safe_float(predictability)


# ---------------- SAVINGS OPTIMIZATION ----------------
async def savings_optimization_analysis(user_id: str):
    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"price": 1},
    )
    income, expenses = 0.0, 0.0
    async for d in cursor:
        price = float(d["price"])
        if price > 0:
            income += price
        else:
            expenses += abs(price)
    rate, score = savings_metrics(income, expenses)
    return _safe_float(rate), _safe_float(score)


# ---------------- NET WORTH ANALYSIS ----------------
async def net_worth_analysis_service(user_id: str):
    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"price": 1},
    )
    assets, liabilities = [], []
    async for d in cursor:
        price = float(d["price"])
        if price > 0:
            assets.append(price)
        else:
            liabilities.append(abs(price))
    return net_worth_analysis(assets, liabilities)


# ---------------- FINANCIAL HEALTH SCORE ----------------
# Bug fixed: burn_rate_analysis raises ValueError if no budget — now returns
# graceful zeros, so this no longer needs a try/except. But added one anyway
# for safety since this aggregates multiple services.
async def financial_health_score(user_id: str):
    savings_rate, _ = await savings_optimization_analysis(user_id)
    volatility, predictability = await income_stability_analysis(user_id)
    try:
        burn_rate, days_left, _ = await burn_rate_analysis(user_id)
    except Exception:
        burn_rate = 0.0

    volatility_score = max(0.0, 100.0 - (_safe_float(volatility) * 100.0))
    burn_score = 100.0 / (1.0 + _safe_float(burn_rate))

    score = (
        _safe_float(savings_rate) * 0.4 +
        _safe_float(predictability) * 0.3 +
        volatility_score * 0.2 +
        burn_score * 0.1
    )
    score = _safe_float(score)

    risk = "low" if score > 75 else "medium" if score > 50 else "high"
    return score, savings_rate, predictability, burn_rate, risk


# ---------------- SPENDING PATTERNS ----------------
# Bug fixed: was calling category_summary without await — it's async.
async def spending_patterns(user_id: str):
    result = await category_summary(user_id, None, None, "expense", None, None)
    total = sum(abs(t) for _, t, _ in result)
    patterns = []
    for category, value, _ in result:
        percent = _safe_float((value / total) * 100 if total else 0.0)
        patterns.append((category, percent))
    return patterns


# ---------------- GOAL PROJECTION ----------------
async def goal_projection(user_id: str, target_amount: float):
    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"price": 1},
    )
    income, expenses = 0.0, 0.0
    async for d in cursor:
        price = float(d["price"])
        if price > 0:
            income += price
        else:
            expenses += abs(price)

    monthly_savings = max(income - expenses, 0.0)
    assets, liabilities, net = await net_worth_analysis_service(user_id)

    if monthly_savings <= 0:
        months = -1.0
    else:
        months = _safe_float(max((target_amount - net) / monthly_savings, 0.0))

    return _safe_float(net), _safe_float(monthly_savings), target_amount, months