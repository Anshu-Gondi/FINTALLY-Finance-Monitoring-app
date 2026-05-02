import io
import logging
import os
from datetime import datetime
from typing import Optional

from bson import ObjectId
from fastapi import APIRouter, Depends, File, Form, HTTPException, Query, UploadFile, status
from fastapi.responses import StreamingResponse

from dependencies.auth import get_current_user
from services.db import db
from services.utils import parse_date, serialize_datetime

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/transaction", tags=["Transactions"])

transactions = db["transactions"]

UPLOAD_DIR = os.path.join(os.path.dirname(__file__), "..", "uploads")
os.makedirs(UPLOAD_DIR, exist_ok=True)


def _serialize(doc: dict) -> dict:
    doc["id"] = str(doc.pop("_id"))
    doc["userId"] = str(doc.get("userId", ""))
    if isinstance(doc.get("datetime"), datetime):
        doc["datetime"] = serialize_datetime(doc["datetime"])  # was .isoformat()
    return doc


# ──────────────────────────────────────────────
# GET /api/transaction/test
# ──────────────────────────────────────────────
@router.get("/test")
async def test():
    return {"body": "test ok"}


# ──────────────────────────────────────────────
# POST /api/transaction  (multipart form)
# ──────────────────────────────────────────────
@router.post("/")
async def create_transaction(
    name: str = Form(...),
    price: float = Form(...),
    description: str = Form(...),
    datetime_: str = Form(..., alias="datetime"),
    category: str = Form("General"),
    isRecurring: bool = Form(False),
    recurringFrequency: Optional[str] = Form(None),
    receipt: Optional[UploadFile] = File(None),
    user: dict = Depends(get_current_user),
):
    receipt_url = None
    if receipt:
        filename = f"{int(datetime.utcnow().timestamp() * 1000)}_{receipt.filename}"
        file_path = os.path.join(UPLOAD_DIR, filename)
        with open(file_path, "wb") as f:
            f.write(await receipt.read())
        receipt_url = f"/uploads/{filename}"

    doc = {
        "name": name,
        "price": price,
        "description": description,
        "datetime": parse_date(datetime_),
        "category": category,
        "isRecurring": isRecurring,
        "recurringFrequency": recurringFrequency if isRecurring else None,
        "userId": ObjectId(user["userId"]),
        "receiptUrl": receipt_url,
        "lastGeneratedAt": None,
    }

    result = await transactions.insert_one(doc)
    doc["_id"] = result.inserted_id
    return {"success": True, "data": _serialize(doc)}


# ──────────────────────────────────────────────
# GET /api/transaction  (paginated)
# ──────────────────────────────────────────────
@router.get("/")
async def get_transactions(
    page: int = Query(1, ge=1),
    user: dict = Depends(get_current_user),
):
    limit = 10
    skip = (page - 1) * limit

    cursor = (
        transactions.find({"userId": ObjectId(user["userId"])})
        .sort("datetime", -1)
        .skip(skip)
        .limit(limit)
    )
    docs = [_serialize(doc) async for doc in cursor]
    return {"success": True, "data": docs}


# ──────────────────────────────────────────────
# PUT /api/transaction/:id
# ──────────────────────────────────────────────
@router.put("/{transaction_id}")
async def update_transaction(
    transaction_id: str,
    name: Optional[str] = Form(None),
    price: Optional[float] = Form(None),
    description: Optional[str] = Form(None),
    datetime_: Optional[str] = Form(None, alias="datetime"),
    category: Optional[str] = Form(None),
    isRecurring: Optional[bool] = Form(None),
    recurringFrequency: Optional[str] = Form(None),
    receipt: Optional[UploadFile] = File(None),
    user: dict = Depends(get_current_user),
):
    existing = await transactions.find_one(
        {"_id": ObjectId(transaction_id), "userId": ObjectId(user["userId"])}
    )
    if not existing:
        raise HTTPException(status_code=404, detail="Transaction not found")

    # Remove old receipt if new one uploaded
    if receipt and existing.get("receiptUrl"):
        old_path = os.path.join(os.path.dirname(__file__), "..", existing["receiptUrl"].lstrip("/"))
        if os.path.exists(old_path):
            os.remove(old_path)

    update: dict = {}
    if name is not None:
        update["name"] = name
    if price is not None:
        update["price"] = price
    if description is not None:
        update["description"] = description
    if datetime_ is not None:
        update["datetime"] = parse_date(datetime_)
    if category is not None:
        update["category"] = category
    if isRecurring is not None:
        update["isRecurring"] = isRecurring
    if recurringFrequency is not None:
        update["recurringFrequency"] = recurringFrequency
    if receipt:
        filename = f"{int(datetime.utcnow().timestamp() * 1000)}_{receipt.filename}"
        file_path = os.path.join(UPLOAD_DIR, filename)
        with open(file_path, "wb") as f:
            f.write(await receipt.read())
        update["receiptUrl"] = f"/uploads/{filename}"

    updated = await transactions.find_one_and_update(
        {"_id": ObjectId(transaction_id), "userId": ObjectId(user["userId"])},
        {"$set": update},
        return_document=True,
    )
    return {"success": True, "data": _serialize(updated)}


# ──────────────────────────────────────────────
# DELETE /api/transaction/:id
# ──────────────────────────────────────────────
@router.delete("/{transaction_id}")
async def delete_transaction(
    transaction_id: str,
    user: dict = Depends(get_current_user),
):
    doc = await transactions.find_one(
        {"_id": ObjectId(transaction_id), "userId": ObjectId(user["userId"])}
    )
    if not doc:
        raise HTTPException(status_code=404, detail="Transaction not found")

    if doc.get("receiptUrl"):
        path = os.path.join(os.path.dirname(__file__), "..", doc["receiptUrl"].lstrip("/"))
        if os.path.exists(path):
            os.remove(path)

    await transactions.delete_one({"_id": ObjectId(transaction_id)})
    return {"success": True, "message": "Transaction deleted successfully"}


# ──────────────────────────────────────────────
# GET /api/transaction/receipt/:id  — PDF receipt
# ──────────────────────────────────────────────
@router.get("/receipt/{transaction_id}")
async def get_receipt(
    transaction_id: str,
    user: dict = Depends(get_current_user),
):
    try:
        from reportlab.lib.pagesizes import A4
        from reportlab.lib import colors
        from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, HRFlowable
        from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
        from reportlab.lib.units import mm
        from reportlab.lib.enums import TA_CENTER, TA_LEFT
    except ImportError:
        raise HTTPException(
            status_code=500,
            detail="reportlab not installed. Run: pip install reportlab",
        )

    doc_data = await transactions.find_one(
        {"_id": ObjectId(transaction_id), "userId": ObjectId(user["userId"])}
    )
    if not doc_data:
        raise HTTPException(status_code=404, detail="Transaction not found")

    buffer = io.BytesIO()
    pdf = SimpleDocTemplate(buffer, pagesize=A4, leftMargin=40, rightMargin=40, topMargin=30, bottomMargin=30)

    CYAN = colors.HexColor("#00f0ff")
    DARK = colors.HexColor("#0d0d0d")
    RED = colors.HexColor("#c70000")
    GREY = colors.HexColor("#888888")

    styles = getSampleStyleSheet()
    brand_style = ParagraphStyle("brand", fontSize=22, textColor=CYAN, fontName="Helvetica-Bold")
    title_style = ParagraphStyle("title", fontSize=16, textColor=DARK, fontName="Helvetica-Bold", alignment=TA_CENTER)
    label_style = ParagraphStyle("label", fontSize=10, textColor=CYAN, fontName="Helvetica-Bold")
    value_style = ParagraphStyle("value", fontSize=10, textColor=DARK, fontName="Helvetica")
    footer_style = ParagraphStyle("footer", fontSize=8, textColor=GREY, alignment=TA_CENTER)

    tx_id = str(doc_data["_id"])
    dt = doc_data.get("datetime")
    dt_str = dt.strftime("%Y-%m-%d %H:%M:%S") if isinstance(dt, datetime) else str(dt)
    price = doc_data.get("price", 0)
    category = doc_data.get("category", "N/A")
    description = doc_data.get("description", "N/A")
    is_recurring = doc_data.get("isRecurring", False)
    frequency = doc_data.get("recurringFrequency", "N/A")

    rows = [
        ["Transaction ID", tx_id],
        ["Date / Time", dt_str],
        ["Category", category],
        ["Description", description],
        ["Recurring", "Yes" if is_recurring else "No"],
    ]
    if is_recurring:
        rows.append(["Frequency", frequency or "N/A"])
    rows.append(["Amount", f"₹{price:.2f}"])

    table = Table(rows, colWidths=[120, 350])
    table.setStyle(TableStyle([
        ("TEXTCOLOR", (0, 0), (0, -1), CYAN),
        ("TEXTCOLOR", (1, 0), (1, -2), DARK),
        ("TEXTCOLOR", (1, -1), (1, -1), RED),
        ("FONTNAME", (0, 0), (0, -1), "Helvetica-Bold"),
        ("FONTNAME", (1, 0), (1, -1), "Helvetica"),
        ("FONTSIZE", (0, 0), (-1, -1), 10),
        ("ROWBACKGROUNDS", (0, 0), (-1, -1), [colors.white, colors.HexColor("#f7f7f7")]),
        ("BOX", (0, 0), (-1, -1), 0.5, CYAN),
        ("LINEBELOW", (0, -1), (-1, -1), 1, CYAN),
        ("TOPPADDING", (0, 0), (-1, -1), 6),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
    ]))

    story = [
        Paragraph("FinTally", brand_style),
        HRFlowable(width="100%", thickness=1, color=CYAN),
        Spacer(1, 8),
        Paragraph("Transaction Receipt", title_style),
        Spacer(1, 12),
        table,
        Spacer(1, 24),
        Paragraph("Thank you for using FinTally!", footer_style),
        Paragraph("This receipt is computer generated and does not require a signature.", footer_style),
    ]

    pdf.build(story)
    buffer.seek(0)

    return StreamingResponse(
        buffer,
        media_type="application/pdf",
        headers={"Content-Disposition": f"attachment; filename=receipt_{tx_id}.pdf"},
    )