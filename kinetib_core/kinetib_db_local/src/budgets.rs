//! Compile-time checked SQLite repository for device-local budget allocations.
//!
//! Queries are owner-scoped and use integer minor units for budget values.
//! Expenses are still stored as REAL in the legacy transaction table; this
//! repository converts their absolute sum to minor units at the aggregation edge.

use crate::LocalDbContext;

pub const MAX_BUDGETS_PER_RESPONSE: i64 = 65;

#[derive(Debug, Clone)]
pub struct BudgetRow {
    pub id: i64,
    pub category: String,
    pub amount_minor_units: i64,
    pub spent_minor_units: i64,
    pub period_start_unix: i64,
    pub period_end_unix: Option<i64>,
    pub is_active: bool,
    pub revision: i64,
}

struct BudgetQueryRow {
    id: i64,
    category: String,
    amount_minor_units: i64,
    spent_minor_units: i64,
    period_start_unix: i64,
    period_end_unix: Option<i64>,
    is_active: bool,
    revision: i64,
}

fn map_row(row: BudgetQueryRow) -> BudgetRow {
    BudgetRow {
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

pub async fn list_budgets(
    db: &LocalDbContext,
    user_id: i64,
) -> Result<Vec<BudgetRow>, sqlx::Error> {
    let rows = sqlx::query_as!(
        BudgetQueryRow,
        r#"
        SELECT
            b.id AS "id!",
            b.category AS "category!",
            b.amount_minor_units AS "amount_minor_units!",
            CAST(COALESCE((
                SELECT COALESCE(SUM(CAST(ROUND(ABS(t.price) * 100.0) AS INTEGER)), 0)
                FROM transactions AS t
                WHERE t.user_id = b.user_id
                  AND t.price < 0
                  AND t.datetime >= b.period_start_unix
                  AND (b.period_end_unix IS NULL OR t.datetime <= b.period_end_unix)
                  AND (b.category = 'Overall' OR t.category = b.category)
            ), 0) AS INTEGER) AS "spent_minor_units!: i64",
            b.period_start_unix AS "period_start_unix!",
            b.period_end_unix AS "period_end_unix?",
            (b.is_active != 0) AS "is_active!: bool",
            b.revision AS "revision!"
        FROM budget_allocations AS b
        WHERE b.user_id = ?1
        ORDER BY b.is_active DESC, b.period_start_unix DESC, b.id DESC
        LIMIT ?2
        "#,
        user_id,
        MAX_BUDGETS_PER_RESPONSE,
    )
    .fetch_all(&db.pool)
    .await?;

    Ok(rows.into_iter().map(map_row).collect())
}

pub async fn create_budget(
    db: &LocalDbContext,
    user_id: i64,
    category: &str,
    amount_minor_units: i64,
    period_start_unix: i64,
    period_end_unix: Option<i64>,
    now_unix: i64,
) -> Result<BudgetRow, sqlx::Error> {
    let mut transaction = db.pool.begin().await?;
    let inserted = sqlx::query!(
        r#"
        INSERT INTO budget_allocations (
            user_id, category, amount_minor_units, period_start_unix,
            period_end_unix, is_active, created_at_unix, updated_at_unix, revision
        ) VALUES (?1, ?2, ?3, ?4, ?5, 1, ?6, ?6, 1)
        RETURNING id AS "id!"
        "#,
        user_id,
        category,
        amount_minor_units,
        period_start_unix,
        period_end_unix,
        now_unix,
    )
    .fetch_one(&mut *transaction)
    .await?;

    for threshold_percent in [80_i64, 100_i64] {
        sqlx::query!(
            r#"
            INSERT INTO budget_threshold_rules (
                user_id, budget_id, threshold_percent, is_enabled,
                created_at_unix, updated_at_unix
            ) VALUES (?1, ?2, ?3, 1, ?4, ?4)
            ON CONFLICT (budget_id, threshold_percent) DO NOTHING
            "#,
            user_id,
            inserted.id,
            threshold_percent,
            now_unix,
        )
        .execute(&mut *transaction)
        .await?;
    }

    let row = fetch_budget(&mut transaction, user_id, inserted.id).await?;
    transaction.commit().await?;
    Ok(row)
}

pub async fn update_budget(
    db: &LocalDbContext,
    user_id: i64,
    budget_id: i64,
    expected_revision: i64,
    category: &str,
    amount_minor_units: i64,
    period_start_unix: i64,
    period_end_unix: Option<i64>,
    now_unix: i64,
) -> Result<Option<BudgetRow>, sqlx::Error> {
    let mut transaction = db.pool.begin().await?;
    let updated = sqlx::query!(
        r#"
        UPDATE budget_allocations
        SET category = ?1,
            amount_minor_units = ?2,
            period_start_unix = ?3,
            period_end_unix = ?4,
            updated_at_unix = ?5,
            revision = revision + 1
        WHERE id = ?6 AND user_id = ?7 AND revision = ?8
        RETURNING id AS "id!"
        "#,
        category,
        amount_minor_units,
        period_start_unix,
        period_end_unix,
        now_unix,
        budget_id,
        user_id,
        expected_revision,
    )
    .fetch_optional(&mut *transaction)
    .await?;

    let Some(updated) = updated else {
        transaction.rollback().await?;
        return Ok(None);
    };

    for threshold_percent in [80_i64, 100_i64] {
        sqlx::query!(
            r#"
            INSERT INTO budget_threshold_rules (
                user_id, budget_id, threshold_percent, is_enabled,
                created_at_unix, updated_at_unix
            ) VALUES (?1, ?2, ?3, 1, ?4, ?4)
            ON CONFLICT (budget_id, threshold_percent) DO NOTHING
            "#,
            user_id,
            updated.id,
            threshold_percent,
            now_unix,
        )
        .execute(&mut *transaction)
        .await?;
    }

    let row = fetch_budget(&mut transaction, user_id, updated.id).await?;
    transaction.commit().await?;
    Ok(Some(row))
}

pub async fn delete_budget(
    db: &LocalDbContext,
    user_id: i64,
    budget_id: i64,
    expected_revision: i64,
) -> Result<bool, sqlx::Error> {
    let row = sqlx::query!(
        r#"
        DELETE FROM budget_allocations
        WHERE id = ?1 AND user_id = ?2 AND revision = ?3
        RETURNING id AS "id!"
        "#,
        budget_id,
        user_id,
        expected_revision,
    )
    .fetch_optional(&db.pool)
    .await?;
    Ok(row.is_some())
}

async fn fetch_budget(
    transaction: &mut sqlx::Transaction<'_, sqlx::Sqlite>,
    user_id: i64,
    budget_id: i64,
) -> Result<BudgetRow, sqlx::Error> {
    let row = sqlx::query_as!(
        BudgetQueryRow,
        r#"
        SELECT
            b.id AS "id!",
            b.category AS "category!",
            b.amount_minor_units AS "amount_minor_units!",
            CAST(COALESCE((
                SELECT COALESCE(SUM(CAST(ROUND(ABS(t.price) * 100.0) AS INTEGER)), 0)
                FROM transactions AS t
                WHERE t.user_id = b.user_id
                  AND t.price < 0
                  AND t.datetime >= b.period_start_unix
                  AND (b.period_end_unix IS NULL OR t.datetime <= b.period_end_unix)
                  AND (b.category = 'Overall' OR t.category = b.category)
            ), 0) AS INTEGER) AS "spent_minor_units!: i64",
            b.period_start_unix AS "period_start_unix!",
            b.period_end_unix AS "period_end_unix?",
            (b.is_active != 0) AS "is_active!: bool",
            b.revision AS "revision!"
        FROM budget_allocations AS b
        WHERE b.user_id = ?1 AND b.id = ?2
        "#,
        user_id,
        budget_id,
    )
    .fetch_one(&mut **transaction)
    .await?;
    Ok(map_row(row))
}
