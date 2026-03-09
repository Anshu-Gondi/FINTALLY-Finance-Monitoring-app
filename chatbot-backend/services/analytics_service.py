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

# ---------------- DAILY SUMMARY ----------------


def daily_summary(user_id: str, interval: int):

    now = datetime.utcnow().replace(tzinfo=UTC)
    start = datetime(now.year, now.month, now.day, tzinfo=UTC)

    cursor = transactions.find(
        {"userId": ObjectId(user_id), "datetime": {
            "$gte": start, "$lte": now}},
        {"datetime": 1, "price": 1},
    )

    ts = []
    prices = []

    for doc in cursor:
        ts.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    return aggregate_by_interval(ts, prices, interval)


# ---------------- PERIOD SUMMARY ----------------
def period_summary(user_id: str, range: str, bucket_days):

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

    return aggregate_by_day(dates, prices, bucket_days)


# ---------------- LIFETIME ----------------
def lifetime_analysis(user_id: str):

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"datetime": 1, "price": 1},
    )

    dates, prices = [], []

    for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    return aggregate_by_month(dates, prices)


# ---------------- CATEGORY ----------------
def category_summary(user_id, start, end, type, keyword, limit):

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

    for doc in cursor:
        categories.append(doc.get("category", "Uncategorized"))
        prices.append(float(doc["price"]))

    return aggregate_by_category(categories, prices, limit)


# ---------------- TREND ----------------
def trend_summary(user_id: str, range: str):

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
        {"userId": ObjectId(user_id), "datetime": {
            "$gte": start, "$lte": now}},
        {"datetime": 1, "price": 1},
    )

    dates, prices = [], []

    for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    return aggregate_trend(dates, prices, mode)


# ---------------- MIN MAX ----------------
def min_max_transaction(user_id: str):

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"datetime": 1, "price": 1},
    )

    dates, prices = [], []

    for doc in cursor:
        dates.append(serialize_datetime(doc["datetime"]))
        prices.append(float(doc["price"]))

    if not dates:
        return None, None

    return find_min_max(dates, prices)


# ---------------- EMI PRESSURE ----------------
def emi_pressure(user_id):

    cursor = transactions.find(
        {"userId": ObjectId(user_id), "emiMeta": {"$exists": True}},
        {"datetime": 1, "emiMeta": 1},
    )

    dates, principals, rates, tenures = [], [], [], []

    for d in cursor:
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

    monthly_income = sum(float(d["price"]) for d in income_cursor)

    score, label = emi_survivability_score(monthly_income, total_emi)

    emi_ratio = total_emi / monthly_income if monthly_income else 0

    return total_emi, emi_ratio, score, label


# ---------------- CASHFLOW FORECAST ----------------
def cashflow_forecast(user_id, horizons):

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"datetime": 1, "price": 1, "recurringFrequency": 1},
    )

    dates, prices, freqs = [], [], []

    for d in cursor:
        dates.append(serialize_datetime(d["datetime"]))
        prices.append(float(d["price"]))
        freqs.append(d.get("recurringFrequency", "none"))

    return rust_cashflow_forecast(dates, prices, freqs, horizons)


# ---------------- BUDGET BREACH ----------------
def budget_breach_prediction(user_id, end_date, simulations):

    budget = get_active_budget(user_id)

    if not budget:
        raise ValueError("No active budget found")

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

    for d in cursor:
        dates.append(serialize_datetime(d["datetime"]))
        prices.append(float(d["price"]))

    return predict_budget_breach(
        dates=dates,
        prices=prices,
        budget_amount=budget_amount,
        horizon_days=horizon_days,
        simulations=simulations,
    )


# ---------------- ANOMALIES ----------------
def recurring_anomalies(user_id):

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"description": 1, "price": 1, "datetime": 1},
    )

    descs, prices, dates = [], [], []

    for d in cursor:
        descs.append(d.get("description", ""))
        prices.append(float(d["price"]))
        dates.append(serialize_datetime(d["datetime"]))

    return detect_recurring_anomalies(descs, prices, dates)

# ---------------- TRANSACTION ANOMALIES ----------------


def anomalies(user_id: str, threshold: float):

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"datetime": 1, "price": 1},
    )

    dates, prices = [], []

    for d in cursor:
        dates.append(serialize_datetime(d["datetime"]))
        prices.append(float(d["price"]))

    return detect_anomalies(dates, prices, threshold)


# ---------------- CATEGORY DRIFT ----------------
def category_drift_analysis(user_id: str):

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"category": 1, "price": 1, "datetime": 1},
    )

    prev = {}
    curr = {}

    now = datetime.utcnow().replace(tzinfo=UTC)
    current_month = now.month

    for d in cursor:

        cat = d.get("category", "Uncategorized")
        price = abs(float(d["price"]))
        dt = d["datetime"]

        if dt.year == now.year and dt.month == now.month:
            curr[cat] = curr.get(cat, 0.0) + price
        else:
            prev[cat] = prev.get(cat, 0.0) + price

    return category_drift(prev, curr)


# ---------------- RECURRING IMPACT ----------------
def recurring_impact_analysis(user_id: str):

    cursor = transactions.find(
        {"userId": ObjectId(user_id), "isRecurring": True},
        {"price": 1, "frequency": 1},
    )

    prices = []
    freqs = []

    for d in cursor:
        prices.append(float(d["price"]))
        freqs.append(d.get("recurringFrequency", "Monthly"))

    return recurring_impact(prices, freqs)


# ---------------- BUDGET UTILIZATION ----------------
def budget_utilization_analysis(user_id: str):

    budget = get_active_budget(user_id)

    if not budget:
        raise ValueError("No active budget found")

    amount = float(budget["amount"])

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"price": 1},
    )

    prices = []

    for d in cursor:
        price = float(d["price"])
        if price < 0:
            prices.append(price)

    spent, remaining, percent = budget_utilization(amount, prices)

    return spent, remaining, percent, amount


# ---------------- BURN RATE ----------------
def burn_rate_analysis(user_id: str):

    budget = get_active_budget(user_id)

    if not budget:
        raise ValueError("No active budget found")

    start = budget["startDate"].replace(tzinfo=UTC)
    amount = float(budget["amount"])

    cursor = transactions.find(
        {
            "userId": ObjectId(user_id),
            "datetime": {"$gte": start},
        },
        {"price": 1},
    )

    prices = []

    for d in cursor:
        prices.append(float(d["price"]))

    spent, remaining, _ = budget_utilization(amount, prices)

    days_elapsed = max((datetime.utcnow().replace(tzinfo=UTC) - start).days, 1)

    burn_rate, days_left = budget_burn_rate(
        spent,
        days_elapsed,
        remaining,
    )

    return burn_rate, days_left, days_elapsed

# ---------------- INCOME STABILITY ----------------


def income_stability_analysis(user_id: str):

    cursor = transactions.find(
        {"userId": ObjectId(user_id), "price": {"$gt": 0}},
        {"price": 1},
    )

    incomes = []

    for d in cursor:
        incomes.append(float(d["price"]))

    return income_stability(incomes)

# ---------------- SAVINGS OPTIMIZATION ----------------


def savings_optimization_analysis(user_id: str):

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"price": 1},
    )

    income = 0.0
    expenses = 0.0

    for d in cursor:
        price = float(d["price"])

        if price > 0:
            income += price
        else:
            expenses += abs(price)

    return savings_metrics(income, expenses)

# ---------------- NET WORTH ANALYSIS ----------------


def net_worth_analysis_service(user_id: str):

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"price": 1},
    )

    assets = []
    liabilities = []

    for d in cursor:
        price = float(d["price"])

        if price > 0:
            assets.append(price)
        else:
            liabilities.append(abs(price))

    return net_worth_analysis(assets, liabilities)

# ---------------- FINANCIAL HEALTH SCORE ----------------


def financial_health_score(user_id):

    savings_rate, _ = savings_optimization_analysis(user_id)
    volatility, predictability = income_stability_analysis(user_id)
    burn_rate, days_left, _ = burn_rate_analysis(user_id)

    # --- NORMALIZATION ---
    volatility_score = max(0, 100 - (volatility * 100))
    burn_score = 100 / (1 + burn_rate)

    score = (
        savings_rate * 0.4 +
        predictability * 0.3 +
        volatility_score * 0.2 +
        burn_score * 0.1
    )

    if score > 75:
        risk = "low"
    elif score > 50:
        risk = "medium"
    else:
        risk = "high"

    return score, savings_rate, predictability, burn_rate, risk

# ---------------- SPENDING PATTERNS ----------------


def spending_patterns(user_id):

    result = category_summary(user_id, None, None, "expense", None, None)

    total = sum(t for _, t, _ in result)

    patterns = []

    for category, value, _ in result:
        percent = (value / total) * 100 if total else 0
        patterns.append((category, percent))

    return patterns

# ---------------- GOAL PROJECTION ----------------


def goal_projection(user_id, target_amount):

    cursor = transactions.find(
        {"userId": ObjectId(user_id)},
        {"price": 1},
    )

    income = 0.0
    expenses = 0.0

    for d in cursor:
        price = float(d["price"])

        if price > 0:
            income += price
        else:
            expenses += abs(price)

    monthly_savings = max(income - expenses, 0)

    assets, liabilities, net = net_worth_analysis_service(user_id)

    if monthly_savings <= 0:
        months = -1
    else:
        months = max((target_amount - net) / monthly_savings, 0)

    return net, monthly_savings, target_amount, months
