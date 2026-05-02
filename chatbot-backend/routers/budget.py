import logging
from datetime import datetime
from typing import Optional

from bson import ObjectId
from fastapi import APIRouter, Depends, HTTPException
from fastapi_cache.decorator import cache
from fastapi_cache import FastAPICache

from dependencies.auth import get_current_user
from schemas.models import BudgetCreate
from services.db import db

# ── Rust extension (fintally_finance) ────────────────────────────────────────
# budget_projection_batch(principals, budgets, spent, months) -> BudgetProjectionResult
# All monetary values in integer currency units (paise = rupees × 100).
# .projected_spent, .usage_percent, .warning_flag  (all same-length lists)
try:
    from fintally_finance import budget_projection_batch as _rust_budget_projection
    _RUST_AVAILABLE = True
except ImportError:
    _RUST_AVAILABLE = False
    _logger_tmp = logging.getLogger(__name__)
    _logger_tmp.warning("fintally_finance Rust extension not found — falling back to Python budget math")

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/budget", tags=["Budget"])

budgets = db["budgets"]
transactions = db["transactions"]

_BUDGET_CACHE_NS = "budget"


async def _invalidate_budget_cache(user_id: str) -> None:
    """Bust cached GET responses for this user after any write."""
    backend = FastAPICache.get_backend()
    for key_suffix in ("list", "summary"):
        key = FastAPICache.get_key_builder()(f"{_BUDGET_CACHE_NS}:{user_id}:{key_suffix}", None, None, None)
        await backend.clear(key=key)


def _budget_cache_key(func, *args, **kwargs) -> str:
    """
    Custom key builder that scopes cache by user ID so different users
    never share each other's budget data.
    """
    request = kwargs.get("request") or (args[0] if args else None)
    user = kwargs.get("user", {})
    uid = user.get("userId", "anon")
    return f"{_BUDGET_CACHE_NS}:{uid}:{func.__name__}"


def _serialize_budget(doc: dict) -> dict:
    doc["id"] = str(doc.pop("_id"))
    doc["userId"] = str(doc.get("userId", ""))
    for field in ("startDate", "endDate", "createdAt"):
        if isinstance(doc.get(field), datetime):
            doc[field] = doc[field].isoformat()
    return doc


# ──────────────────────────────────────────────
# POST /api/budget  — create or update
# ──────────────────────────────────────────────
@router.post("/")
async def create_or_update_budget(
    body: BudgetCreate,
    user: dict = Depends(get_current_user),
):
    user_id = ObjectId(user["userId"])
    start = body.startDate or datetime.utcnow()
    end = body.endDate

    # Calculate spent for the category
    match_query: dict = {
        "userId": user_id,
        "price": {"$lt": 0},
        "datetime": {"$gte": start},
    }
    if body.category != "Overall":
        match_query["category"] = body.category
    if end:
        match_query["datetime"]["$lte"] = end

    pipeline = [
        {"$match": match_query},
        {"$group": {"_id": None, "total": {"$sum": {"$abs": "$price"}}}},
    ]
    agg = await transactions.aggregate(pipeline).to_list(1)
    spent = agg[0]["total"] if agg else 0.0

    # Check for overlapping budget for same category
    overlap_filter: dict = {
        "userId": user_id,
        "category": body.category,
        "startDate": {"$lte": end or datetime.utcnow()},
        "$or": [{"endDate": None}, {"endDate": {"$gte": start}}],
    }
    existing = await budgets.find_one(overlap_filter)

    if existing:
        await budgets.update_one(
            {"_id": existing["_id"]},
            {
                "$set": {
                    "amount": body.amount,
                    "startDate": start,
                    "endDate": end,
                    "isRecurring": body.isRecurring,
                    "spent": spent,
                }
            },
        )
        updated = await budgets.find_one({"_id": existing["_id"]})
        await _invalidate_budget_cache(str(user_id))
        return {"success": True, "data": _serialize_budget(updated)}

    doc = {
        "userId": user_id,
        "amount": body.amount,
        "category": body.category,
        "startDate": start,
        "endDate": end,
        "isRecurring": body.isRecurring,
        "spent": spent,
        "createdAt": datetime.utcnow(),
    }
    result = await budgets.insert_one(doc)
    doc["_id"] = result.inserted_id
    await _invalidate_budget_cache(str(user_id))
    return {"success": True, "data": _serialize_budget(doc)}


# ──────────────────────────────────────────────
# GET /api/budget
# Cached per-user, TTL 2 min. Invalidated on write.
# ──────────────────────────────────────────────
@router.get("/")
@cache(expire=120, namespace="budget_list", key_builder=_budget_cache_key)
async def get_budgets(user: dict = Depends(get_current_user)):
    cursor = budgets.find({"userId": ObjectId(user["userId"])}).sort("createdAt", -1)
    docs = [_serialize_budget(doc) async for doc in cursor]
    return {"success": True, "data": docs}


# ──────────────────────────────────────────────
# GET /api/budget/summary
# Cached per-user, TTL 2 min. Invalidated on write.
# Uses Rust budget_projection_batch for remaining/usage math when available.
# ──────────────────────────────────────────────
@router.get("/summary")
@cache(expire=120, namespace="budget_summary", key_builder=_budget_cache_key)
async def budget_summary(user: dict = Depends(get_current_user)):
    user_id = ObjectId(user["userId"])

    user_budgets = await budgets.find({"userId": user_id}).to_list(None)

    # One aggregation for all expenses grouped by category
    expense_pipeline = [
        {"$match": {"userId": user_id, "price": {"$lt": 0}}},
        {"$group": {"_id": "$category", "total": {"$sum": {"$abs": "$price"}}}},
    ]
    expense_agg = await transactions.aggregate(expense_pipeline).to_list(None)
    expense_map = {e["_id"]: e["total"] for e in expense_agg}
    total_spent = sum(expense_map.values())

    if not user_budgets:
        return {"success": True, "data": []}

    # Build per-budget spent values
    spent_per_budget: list[float] = []
    for b in user_budgets:
        if b["category"] == "Overall":
            spent_per_budget.append(total_spent)
        else:
            spent_per_budget.append(expense_map.get(b["category"], 0.0))

    # ── Rust path: budget_projection_batch for remaining/usage ───────────────
    # We pass monthly_spend_rate = spent (current snapshot, months=1 so no
    # projection needed — we just want the usage% and warning flag on current data).
    # Rust signature: (principals, budgets, spent, months) all in paise (int).
    if _RUST_AVAILABLE:
        budget_amounts_paise = [int(round(b["amount"] * 100)) for b in user_budgets]
        spent_paise          = [int(round(s * 100)) for s in spent_per_budget]
        # principals = 0 because we're not projecting forward, just scoring now
        zero_rates           = [0] * len(user_budgets)
        one_month            = [1] * len(user_budgets)

        rust_result = _rust_budget_projection(
            zero_rates,            # monthly spend rate (0 = no future projection)
            budget_amounts_paise,
            spent_paise,
            one_month,
        )
        # warning_flag: 0=healthy, 1=approaching (80–99%), 2=over budget
        _WARNING_LABELS = {0: "healthy", 1: "warning", 2: "over_budget"}

        summaries = []
        for i, b in enumerate(user_budgets):
            start_str = b["startDate"].strftime("%Y-%m-%d") if isinstance(b.get("startDate"), datetime) else "N/A"
            end_str   = b["endDate"].strftime("%Y-%m-%d")   if isinstance(b.get("endDate"),   datetime) else "Ongoing"
            remaining = max(b["amount"] - spent_per_budget[i], 0)
            summaries.append({
                "category":    b["category"],
                "budget":      b["amount"],
                "spent":       spent_per_budget[i],
                "remaining":   remaining,
                "usagePercent": round(rust_result.usage_percent[i], 2),
                "status":       _WARNING_LABELS.get(rust_result.warning_flag[i], "healthy"),
                "period":      f"{start_str} - {end_str}",
            })
        return {"success": True, "data": summaries}

    # ── Python fallback ───────────────────────────────────────────────────────
    summaries = []
    for i, b in enumerate(user_budgets):
        spent = spent_per_budget[i]
        start_str = b["startDate"].strftime("%Y-%m-%d") if isinstance(b.get("startDate"), datetime) else "N/A"
        end_str   = b["endDate"].strftime("%Y-%m-%d")   if isinstance(b.get("endDate"),   datetime) else "Ongoing"
        remaining = max(b["amount"] - spent, 0)
        usage_pct = round((spent / b["amount"] * 100) if b["amount"] > 0 else 100.0, 2)
        summaries.append({
            "category":    b["category"],
            "budget":      b["amount"],
            "spent":       spent,
            "remaining":   remaining,
            "usagePercent": usage_pct,
            "status":      "over_budget" if usage_pct >= 100 else "warning" if usage_pct >= 80 else "healthy",
            "period":      f"{start_str} - {end_str}",
        })
    return {"success": True, "data": summaries}


# ──────────────────────────────────────────────
# DELETE /api/budget/:id
# ──────────────────────────────────────────────
@router.delete("/{budget_id}")
async def delete_budget(
    budget_id: str,
    user: dict = Depends(get_current_user),
):
    result = await budgets.find_one_and_delete(
        {"_id": ObjectId(budget_id), "userId": ObjectId(user["userId"])}
    )
    if not result:
        raise HTTPException(status_code=404, detail="Budget not found")
    await _invalidate_budget_cache(user["userId"])
    return {"success": True, "message": "Budget deleted successfully"}