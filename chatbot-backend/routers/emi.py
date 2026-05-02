import logging
from datetime import datetime

from bson import ObjectId
from fastapi import APIRouter, Depends, HTTPException
from fastapi_cache.decorator import cache

from dependencies.auth import get_current_user
from schemas.models import EmiCalculateRequest, EmiCheckRequest, EmiCreateRequest
from services.db import db

# ── Rust extension (fintally_finance) ────────────────────────────────────────
# emi_batch(principals: list[int], rates: list[float], months: list[int]) -> list[int]
# All principal/EMI values are in integer currency units (paise = rupees × 100).
try:
    from fintally_finance import emi_batch as _rust_emi_batch
    _RUST_AVAILABLE = True
except ImportError:
    _RUST_AVAILABLE = False
    logger_tmp = logging.getLogger(__name__)
    logger_tmp.warning("fintally_finance Rust extension not found — falling back to Python EMI math")

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/emi", tags=["EMI"])

transactions = db["transactions"]
budgets = db["budgets"]


# ──────────────────────────────────────────────
# Pure math helpers
# ──────────────────────────────────────────────

def calculate_emi(principal: float, annual_rate: float, months: int) -> float:
    """
    Returns EMI in rupees (float, 2 dp).
    Uses the Rust emi_batch when available; falls back to pure Python.

    Rust contract: inputs in paise (int), output in paise (int).
    Conversion: principal_paise = round(principal * 100)
                emi_rupees      = result_paise / 100
    """
    if _RUST_AVAILABLE:
        principal_paise = int(round(principal * 100))
        result_paise = _rust_emi_batch(
            [principal_paise],
            [float(annual_rate)],
            [int(months)],
        )
        return round(result_paise[0] / 100, 2)

    # Python fallback
    if annual_rate == 0:
        return round(principal / months, 2)
    r = annual_rate / (12 * 100)
    emi = (principal * r * (1 + r) ** months) / ((1 + r) ** months - 1)
    return round(emi, 2)


async def check_emi_affordability(
    user_id: ObjectId,
    principal: float,
    annual_rate: float,
    months: int,
    category: str,
) -> dict:
    """
    Mirrors the Node.js emiBudget.service logic:
    Looks up the user's budget for the given category (or Overall),
    calculates the monthly EMI, and checks whether it fits.
    """
    emi = calculate_emi(principal, annual_rate, months)

    # Find relevant budget
    budget_doc = await budgets.find_one(
        {
            "userId": user_id,
            "$or": [{"category": category}, {"category": "Overall"}],
        }
    )

    if not budget_doc:
        return {
            "affordable": True,
            "emi": emi,
            "budgetLimit": None,
            "remainingBudget": None,
            "message": "No budget set — EMI created without budget check.",
        }

    budget_limit = budget_doc["amount"]

    # Sum existing monthly expenses for this category
    pipeline = [
        {
            "$match": {
                "userId": user_id,
                "price": {"$lt": 0},
                **({"category": category} if category != "Overall" else {}),
            }
        },
        {"$group": {"_id": None, "total": {"$sum": {"$abs": "$price"}}}},
    ]
    agg = await transactions.aggregate(pipeline).to_list(1)
    current_spent = agg[0]["total"] if agg else 0.0

    remaining = budget_limit - current_spent
    affordable = emi <= remaining

    return {
        "affordable": affordable,
        "emi": emi,
        "budgetLimit": budget_limit,
        "currentSpent": current_spent,
        "remainingBudget": remaining,
        "message": (
            "EMI fits within your budget."
            if affordable
            else f"EMI of ₹{emi:.2f} exceeds remaining budget of ₹{remaining:.2f}."
        ),
    }


# ──────────────────────────────────────────────
# POST /api/emi/calculate
# Cached: pure math, no DB reads. TTL 10 min.
# Key is derived from the request body by fastapi-cache.
# ──────────────────────────────────────────────
@router.post("/calculate")
@cache(expire=600, namespace="emi_calculate")
async def emi_calculate(
    body: EmiCalculateRequest,
    user: dict = Depends(get_current_user),
):
    emi = calculate_emi(body.principal, body.annualRate, body.months)
    return {
        "success": True,
        "data": {
            "emi": emi,
            "totalPayable": round(emi * body.months, 2),
            "totalInterest": round(emi * body.months - body.principal, 2),
        },
    }


# ──────────────────────────────────────────────
# POST /api/emi/check
# ──────────────────────────────────────────────
@router.post("/check")
async def emi_check(
    body: EmiCheckRequest,
    user: dict = Depends(get_current_user),
):
    emi = calculate_emi(body.principal, body.annualRate, body.months)
    budget_impact = await check_emi_affordability(
        user_id=ObjectId(user["userId"]),
        principal=body.principal,
        annual_rate=body.annualRate,
        months=body.months,
        category=body.category,
    )
    return {
        "success": True,
        "data": {
            "emi": emi,
            "totalPayable": round(emi * body.months, 2),
            "totalInterest": round(emi * body.months - body.principal, 2),
            "budgetImpact": budget_impact,
        },
    }


# ──────────────────────────────────────────────
# POST /api/emi/create
# ──────────────────────────────────────────────
@router.post("/create")
async def emi_create(
    body: EmiCreateRequest,
    user: dict = Depends(get_current_user),
):
    emi = calculate_emi(body.principal, body.annualRate, body.months)
    budget_impact = await check_emi_affordability(
        user_id=ObjectId(user["userId"]),
        principal=body.principal,
        annual_rate=body.annualRate,
        months=body.months,
        category=body.category,
    )

    if not budget_impact["affordable"]:
        raise HTTPException(
            status_code=400,
            detail={"message": "EMI exceeds your budget", "budgetImpact": budget_impact},
        )

    doc = {
        "name": body.name,
        "price": -emi,  # expense
        "description": f"EMI for loan ({body.months} months @ {body.annualRate}%)",
        "datetime": datetime.utcnow(),
        "category": body.category,
        "userId": ObjectId(user["userId"]),
        "isRecurring": True,
        "recurringFrequency": "Monthly",
        "lastGeneratedAt": None,
        "emiMeta": {
            "principal": body.principal,
            "annualRate": body.annualRate,
            "tenureMonths": body.months,
            "originalTenure": body.months,
        },
    }

    result = await transactions.insert_one(doc)

    return {
        "success": True,
        "message": "EMI added as recurring transaction",
        "data": {
            "emi": emi,
            "transactionId": str(result.inserted_id),
            "budgetImpact": budget_impact,
        },
    }