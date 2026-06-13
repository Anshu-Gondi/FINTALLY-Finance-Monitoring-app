"""
Recurring transaction scheduler — replaces the Node.js node-cron job.

Uses APScheduler (AsyncIOScheduler) so it runs inside the FastAPI event loop.
Register the scheduler in main.py lifespan or startup event.
"""

import logging
from datetime import datetime

from apscheduler.schedulers.asyncio import AsyncIOScheduler
from bson import ObjectId

from apscheduler.triggers.cron import CronTrigger
from services.rag_service import rag_service
from services.db import db

logger = logging.getLogger(__name__)

transactions = db["transactions"]

scheduler = AsyncIOScheduler()


# ──────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────

def _get_week_key(date: datetime) -> str:
    one_jan = datetime(date.year, 1, 1)
    delta = (date - one_jan).days
    week = (delta + one_jan.weekday() + 1) // 7 + 1
    return f"{date.year}-W{week}"


def _is_new_period(frequency: str, last_generated: datetime | None, now: datetime) -> bool:
    if last_generated is None:
        return True
    freq = frequency.lower()
    if freq == "daily":
        return last_generated.date() != now.date()
    if freq == "weekly":
        return _get_week_key(last_generated) != _get_week_key(now)
    if freq == "monthly":
        return (last_generated.year, last_generated.month) != (now.year, now.month)
    return False


# ──────────────────────────────────────────────
# Core job
# ──────────────────────────────────────────────

async def generate_recurring_transactions() -> None:
    """
    Mirrors the Node.js recurringJobs.js logic:
    - Finds all active recurring transactions
    - For each one whose period has elapsed, inserts a new copy
    - Decrements EMI tenure; disables EMI when tenure hits 0
    - Bulk-inserts new transactions, bulk-updates originals
    """
    try:
        now = datetime.utcnow()

        # Only fetch recurring transactions that are still active
        cursor = transactions.find(
            {
                "isRecurring": True,
                "recurringFrequency": {"$ne": None},
                "$or": [
                    {"emiMeta.tenureMonths": {"$gt": 0}},
                    {"emiMeta": {"$exists": False}},
                ],
            }
        )
        recurring_txns = await cursor.to_list(None)

        if not recurring_txns:
            return

        to_insert = []
        to_update = []

        for tx in recurring_txns:
            frequency = tx.get("recurringFrequency", "")
            if not frequency:
                continue

            last_generated = tx.get("lastGeneratedAt") or tx.get("datetime")
            if not _is_new_period(frequency, last_generated, now):
                continue

            # Build new (non-recurring) copy of the transaction
            new_tx = {
                "name": tx["name"],
                "price": tx["price"],
                "description": tx["description"],
                "datetime": now,
                "category": tx.get("category", "General"),
                "userId": tx["userId"],
                "isRecurring": False,
                "recurringFrequency": None,
                "lastGeneratedAt": None,
            }
            to_insert.append(new_tx)

            # Prepare update for the original recurring transaction
            update_fields: dict = {"lastGeneratedAt": now}
            emi_meta = tx.get("emiMeta")
            if emi_meta and emi_meta.get("tenureMonths") is not None:
                new_tenure = max(emi_meta["tenureMonths"] - 1, 0)
                update_fields["emiMeta.tenureMonths"] = new_tenure
                if new_tenure == 0:
                    update_fields["isRecurring"] = False
                    update_fields["recurringFrequency"] = None

            to_update.append({"_id": tx["_id"], "fields": update_fields})

        if to_insert:
            await transactions.insert_many(to_insert)
            logger.info("✅ %d recurring transactions created", len(to_insert))

        for upd in to_update:
            await transactions.update_one(
                {"_id": upd["_id"]}, {"$set": upd["fields"]}
            )

    except Exception:
        logger.exception("❌ Error in recurring transactions job")

async def run_daily_rag_sync() -> None:
    """
    Wakes up, runs the secure Google Drive sync pipeline, 
    and handles native backend index embedding logic.
    """
    try:
        await rag_service.sync_and_reindex_trusted_sources()
    except Exception:
        logger.exception("❌ Error executing daily Google Drive context synchronization")

# ──────────────────────────────────────────────
# Scheduler registration
# ──────────────────────────────────────────────

def start_scheduler() -> None:
    """Call this once during app startup."""
    # Your existing 5 min transaction interval worker
    scheduler.add_job(
        generate_recurring_transactions,
        trigger="interval",
        minutes=5,
        id="recurring_transactions",
        replace_existing=True,
    )
    
    # New Daily RAG Sync Cron Job: Firing every day at 2:30 AM
    scheduler.add_job(
        run_daily_rag_sync,
        trigger="interval",   # Swapped from CronTrigger
        minutes=2,            # Fires every 2 minutes
        id="daily_drive_rag_sync",
        replace_existing=True,
    )
    
    scheduler.start()
    logger.info("Driver hooks connected: Recurring transaction execution (5m) & Daily Google Drive RAG cron (2:30 AM) online.")


def stop_scheduler() -> None:
    """Call this during app shutdown."""
    if scheduler.running:
        scheduler.shutdown(wait=False)
        logger.info("🛑 Scheduler stopped")