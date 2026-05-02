from pydantic import BaseModel, EmailStr, Field
from typing import Optional
from enum import Enum
from datetime import datetime


# ──────────────────────────────────────────────
# Auth
# ──────────────────────────────────────────────

class SignupRequest(BaseModel):
    name: str
    email: EmailStr
    password: str


class LoginRequest(BaseModel):
    email: EmailStr
    password: str


class GoogleAuthRequest(BaseModel):
    token: str


class AuthResponse(BaseModel):
    success: bool
    token: Optional[str] = None
    message: Optional[str] = None


# ──────────────────────────────────────────────
# Transaction
# ──────────────────────────────────────────────

class RecurringFrequency(str, Enum):
    daily = "Daily"
    weekly = "Weekly"
    monthly = "Monthly"


class TransactionCreate(BaseModel):
    name: str
    price: float
    description: str
    datetime: datetime
    category: str = "General"
    isRecurring: bool = False
    recurringFrequency: Optional[RecurringFrequency] = None


class TransactionUpdate(BaseModel):
    name: Optional[str] = None
    price: Optional[float] = None
    description: Optional[str] = None
    datetime: Optional[datetime] = None
    category: Optional[str] = None
    isRecurring: Optional[bool] = None
    recurringFrequency: Optional[RecurringFrequency] = None


# ──────────────────────────────────────────────
# Budget
# ──────────────────────────────────────────────

class BudgetCreate(BaseModel):
    amount: float
    category: str = "Overall"
    startDate: Optional[datetime] = None
    endDate: Optional[datetime] = None
    isRecurring: bool = False


# ──────────────────────────────────────────────
# EMI
# ──────────────────────────────────────────────

class EmiCalculateRequest(BaseModel):
    principal: float = Field(..., gt=0)
    annualRate: float = Field(..., gt=0)
    months: int = Field(..., gt=0)


class EmiCheckRequest(EmiCalculateRequest):
    category: str = "Overall"


class EmiCreateRequest(EmiCalculateRequest):
    category: str = "EMI"
    name: str = "Loan EMI"


# ──────────────────────────────────────────────
# Feedback
# ──────────────────────────────────────────────

class FeedbackCreate(BaseModel):
    name: str
    email: EmailStr
    message: str