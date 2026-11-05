//! Bounded budget IPC DTOs. Amounts are integer minor currency units.

use serde::{Deserialize, Serialize};

#[derive(Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct CreateBudgetRequest {
    pub category: String,
    pub amount_minor_units: i64,
    pub period_start_unix: i64,
    pub period_end_unix: Option<i64>,
}

#[derive(Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct UpdateBudgetRequest {
    pub id: i64,
    pub expected_revision: i64,
    pub category: String,
    pub amount_minor_units: i64,
    pub period_start_unix: i64,
    pub period_end_unix: Option<i64>,
}

#[derive(Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct DeleteBudgetRequest {
    pub id: i64,
    pub expected_revision: i64,
}

#[derive(Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct BudgetRecord {
    pub id: i64,
    pub category: String,
    pub amount_minor_units: i64,
    pub spent_minor_units: i64,
    pub remaining_minor_units: i64,
    /// Percentage multiplied by 100; 8_025 means 80.25%.
    pub utilization_percent_x100: i64,
    pub period_start_unix: i64,
    pub period_end_unix: Option<i64>,
    pub is_active: bool,
    pub revision: i64,
}

#[derive(Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct BudgetPage {
    pub items: Vec<BudgetRecord>,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct DeleteBudgetResult {
    pub budget_id: i64,
    pub deleted: bool,
}
