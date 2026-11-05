//! Budget application service shared by LocalOnly and CloudSync sessions.
//!
//! This module is the only application-layer entry point for budget CRUD.
//! It accepts an authenticated principal supplied by the kernel and never
//! chooses or falls back between storage backends.

pub use kinetib_budget_domain::*;

use crate::auth::AuthenticatedPrincipal;
use kinetib_db::DbContext;
use kinetib_db_local::LocalDbContext;
use thiserror::Error;

pub const MAX_BUDGETS_PER_RESPONSE: usize = 64;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BudgetView {
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BudgetDeleteResult {
    pub budget_id: i64,
    pub deleted: bool,
}

#[derive(Debug, Error, Clone, PartialEq, Eq)]
pub enum BudgetError {
    #[error("invalid budget input")]
    InvalidInput,
    #[error("budget was not found or has changed; refresh and try again")]
    NotFoundOrStale,
    #[error("an active budget already overlaps this category and period")]
    PeriodConflict,
    #[error("budget storage operation failed")]
    Database,
    #[error("stored budget data is invalid")]
    InvalidStoredRecord,
    #[error("budget response exceeds the bounded IPC response limit")]
    ResponseTooLarge,
}

/// List budgets for exactly the authenticated CloudSync principal.
pub async fn list_budgets(
    db: &DbContext,
    principal: &AuthenticatedPrincipal,
) -> Result<Vec<BudgetView>, BudgetError> {
    let rows = kinetib_db::budgets::list_budgets(&db.pool, principal.user_id())
        .await
        .map_err(map_pg_error)?;
    build_views(rows.into_iter().map(RawBudget::from))
}

/// List budgets for exactly the authenticated LocalOnly principal.
pub async fn list_budgets_local(
    db: &LocalDbContext,
    principal: &AuthenticatedPrincipal,
) -> Result<Vec<BudgetView>, BudgetError> {
    let rows = kinetib_db_local::budgets::list_budgets(db, principal.user_id())
        .await
        .map_err(map_sqlite_error)?;
    build_views(rows.into_iter().map(RawBudget::from))
}

pub async fn create_budget(
    db: &DbContext,
    principal: &AuthenticatedPrincipal,
    input: NewBudget,
    now_unix: i64,
) -> Result<BudgetView, BudgetError> {
    let (category, input) = validate_new_budget(input)?;
    let row = kinetib_db::budgets::create_budget(
        &db.pool,
        principal.user_id(),
        &category,
        input.amount_minor_units,
        input.period_start_unix,
        input.period_end_unix,
        now_unix,
    )
    .await
    .map_err(map_pg_error)?;
    map_pg_row(row.into())
}

pub async fn create_budget_local(
    db: &LocalDbContext,
    principal: &AuthenticatedPrincipal,
    input: NewBudget,
    now_unix: i64,
) -> Result<BudgetView, BudgetError> {
    let (category, input) = validate_new_budget(input)?;
    let row = kinetib_db_local::budgets::create_budget(
        db,
        principal.user_id(),
        &category,
        input.amount_minor_units,
        input.period_start_unix,
        input.period_end_unix,
        now_unix,
    )
    .await
    .map_err(map_sqlite_error)?;
    map_local_row(row.into())
}

pub async fn update_budget(
    db: &DbContext,
    principal: &AuthenticatedPrincipal,
    budget_id: i64,
    expected_revision: i64,
    input: NewBudget,
    now_unix: i64,
) -> Result<BudgetView, BudgetError> {
    if budget_id <= 0 || expected_revision <= 0 {
        return Err(BudgetError::InvalidInput);
    }
    let (category, input) = validate_new_budget(input)?;
    let row = kinetib_db::budgets::update_budget(
        &db.pool,
        principal.user_id(),
        budget_id,
        expected_revision,
        &category,
        input.amount_minor_units,
        input.period_start_unix,
        input.period_end_unix,
        now_unix,
    )
    .await
    .map_err(map_pg_error)?
    .ok_or(BudgetError::NotFoundOrStale)?;
    map_pg_row(row.into())
}

pub async fn update_budget_local(
    db: &LocalDbContext,
    principal: &AuthenticatedPrincipal,
    budget_id: i64,
    expected_revision: i64,
    input: NewBudget,
    now_unix: i64,
) -> Result<BudgetView, BudgetError> {
    if budget_id <= 0 || expected_revision <= 0 {
        return Err(BudgetError::InvalidInput);
    }
    let (category, input) = validate_new_budget(input)?;
    let row = kinetib_db_local::budgets::update_budget(
        db,
        principal.user_id(),
        budget_id,
        expected_revision,
        &category,
        input.amount_minor_units,
        input.period_start_unix,
        input.period_end_unix,
        now_unix,
    )
    .await
    .map_err(map_sqlite_error)?
    .ok_or(BudgetError::NotFoundOrStale)?;
    map_local_row(row.into())
}

pub async fn delete_budget(
    db: &DbContext,
    principal: &AuthenticatedPrincipal,
    budget_id: i64,
    expected_revision: i64,
) -> Result<BudgetDeleteResult, BudgetError> {
    if budget_id <= 0 || expected_revision <= 0 {
        return Err(BudgetError::InvalidInput);
    }
    let deleted = kinetib_db::budgets::delete_budget(
        &db.pool,
        principal.user_id(),
        budget_id,
        expected_revision,
    )
    .await
    .map_err(map_pg_error)?;
    if !deleted {
        return Err(BudgetError::NotFoundOrStale);
    }
    Ok(BudgetDeleteResult { budget_id, deleted })
}

pub async fn delete_budget_local(
    db: &LocalDbContext,
    principal: &AuthenticatedPrincipal,
    budget_id: i64,
    expected_revision: i64,
) -> Result<BudgetDeleteResult, BudgetError> {
    if budget_id <= 0 || expected_revision <= 0 {
        return Err(BudgetError::InvalidInput);
    }
    let deleted = kinetib_db_local::budgets::delete_budget(
        db,
        principal.user_id(),
        budget_id,
        expected_revision,
    )
    .await
    .map_err(map_sqlite_error)?;
    if !deleted {
        return Err(BudgetError::NotFoundOrStale);
    }
    Ok(BudgetDeleteResult { budget_id, deleted })
}

fn validate_new_budget(input: NewBudget) -> Result<(String, NewBudget), BudgetError> {
    input.validate().map_err(|_| BudgetError::InvalidInput)?;
    let category = input
        .normalized_category()
        .map_err(|_| BudgetError::InvalidInput)?;
    Ok((category, input))
}

#[derive(Debug)]
struct RawBudget {
    id: i64,
    category: String,
    amount_minor_units: i64,
    spent_minor_units: i64,
    period_start_unix: i64,
    period_end_unix: Option<i64>,
    is_active: bool,
    revision: i64,
}

impl From<kinetib_db::budgets::BudgetRow> for RawBudget {
    fn from(row: kinetib_db::budgets::BudgetRow) -> Self {
        Self {
            id: row.id,
            category: row.category,
            amount_minor_units: row.amount_minor_units,
            spent_minor_units: row.spent_minor_units,
            period_start_unix: row.period_start_unix,
            period_end_unix: row.period_end_unix,
            is_active: row.is_active,
            revision: row.revision,
        }
    }
}

impl From<kinetib_db_local::budgets::BudgetRow> for RawBudget {
    fn from(row: kinetib_db_local::budgets::BudgetRow) -> Self {
        Self {
            id: row.id,
            category: row.category,
            amount_minor_units: row.amount_minor_units,
            spent_minor_units: row.spent_minor_units,
            period_start_unix: row.period_start_unix,
            period_end_unix: row.period_end_unix,
            is_active: row.is_active,
            revision: row.revision,
        }
    }
}

fn build_views<I>(rows: I) -> Result<Vec<BudgetView>, BudgetError>
where
    I: IntoIterator<Item = RawBudget>,
{
    let mut views = Vec::new();
    for row in rows {
        if views.len() == MAX_BUDGETS_PER_RESPONSE {
            return Err(BudgetError::ResponseTooLarge);
        }
        views.push(map_raw(row)?);
    }
    Ok(views)
}

fn map_pg_row(row: RawBudget) -> Result<BudgetView, BudgetError> {
    map_raw(row)
}

fn map_local_row(row: RawBudget) -> Result<BudgetView, BudgetError> {
    map_raw(row)
}

fn map_raw(row: RawBudget) -> Result<BudgetView, BudgetError> {
    if row.id <= 0
        || row.category.trim().is_empty()
        || row.category.len() > MAX_BUDGET_CATEGORY_BYTES
        || row.amount_minor_units <= 0
        || row.spent_minor_units < 0
        || row
            .period_end_unix
            .is_some_and(|end| end < row.period_start_unix)
        || row.revision <= 0
    {
        return Err(BudgetError::InvalidStoredRecord);
    }

    let remaining_minor_units = row
        .amount_minor_units
        .saturating_sub(row.spent_minor_units)
        .max(0);
    let ratio = (i128::from(row.spent_minor_units) * 10_000) / i128::from(row.amount_minor_units);
    let utilization_percent_x100 = i64::try_from(ratio.min(i128::from(i64::MAX)))
        .map_err(|_| BudgetError::InvalidStoredRecord)?;

    Ok(BudgetView {
        id: row.id,
        category: row.category,
        amount_minor_units: row.amount_minor_units,
        spent_minor_units: row.spent_minor_units,
        remaining_minor_units,
        utilization_percent_x100,
        period_start_unix: row.period_start_unix,
        period_end_unix: row.period_end_unix,
        is_active: row.is_active,
        revision: row.revision,
    })
}

fn map_pg_error(error: sqlx::Error) -> BudgetError {
    match &error {
        sqlx::Error::Database(database) => {
            if database.code().as_deref() == Some("23P01")
                || database.message().contains("budget period overlaps")
            {
                BudgetError::PeriodConflict
            } else {
                BudgetError::Database
            }
        }
        _ => BudgetError::Database,
    }
}

fn map_sqlite_error(error: sqlx::Error) -> BudgetError {
    match &error {
        sqlx::Error::Database(database) if database.message().contains("budget_period_overlap") => {
            BudgetError::PeriodConflict
        }
        _ => BudgetError::Database,
    }
}

#[cfg(test)]
mod tests {
    use super::{map_raw, BudgetError};

    #[test]
    fn budget_view_uses_integer_minor_unit_arithmetic() {
        let row = super::RawBudget {
            id: 1,
            category: "Food".into(),
            amount_minor_units: 10_000,
            spent_minor_units: 8_025,
            period_start_unix: 1_800_000_000,
            period_end_unix: None,
            is_active: true,
            revision: 1,
        };
        let view = map_raw(row).unwrap();
        assert_eq!(view.remaining_minor_units, 1_975);
        assert_eq!(view.utilization_percent_x100, 8_025);
    }

    #[test]
    fn invalid_persisted_budget_fails_closed() {
        let row = super::RawBudget {
            id: 0,
            category: "Food".into(),
            amount_minor_units: 10_000,
            spent_minor_units: 0,
            period_start_unix: 1,
            period_end_unix: None,
            is_active: true,
            revision: 1,
        };
        assert_eq!(map_raw(row), Err(BudgetError::InvalidStoredRecord));
    }
}
