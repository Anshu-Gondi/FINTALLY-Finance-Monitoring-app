//! Compile-time checked PostgreSQL repository for budget allocations.
//!
//! All access is scoped to the authenticated owner ID supplied by the kernel.
//! Financial values are persisted in integer minor units. Expense aggregation
//! is converted from the legacy transaction REAL/DOUBLE column at this boundary.

use sqlx::PgPool;

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

/// Lists at most 65 records so the application can reject an oversized page
/// rather than silently truncating a user's budget collection.
pub async fn list_budgets(pool: &PgPool, user_id: i64) -> Result<Vec<BudgetRow>, sqlx::Error> {
    let rows = sqlx::query_as!(
        BudgetQueryRow,
        r#"
        SELECT
            b.id AS "id!",
            b.category AS "category!",
            b.amount_minor_units AS "amount_minor_units!",
            COALESCE((
                SELECT COALESCE(SUM(ROUND(ABS(t.price::numeric) * 100)), 0)::bigint
                FROM transactions AS t
                WHERE t.user_id = b.user_id
                  AND t.price < 0
                  AND t.datetime >= b.period_start_unix
                  AND (b.period_end_unix IS NULL OR t.datetime <= b.period_end_unix)
                  AND (b.category = 'Overall' OR t.category = b.category)
            ), 0)::bigint AS "spent_minor_units!",
            b.period_start_unix AS "period_start_unix!",
            b.period_end_unix AS "period_end_unix?",
            b.is_active AS "is_active!",
            b.revision AS "revision!"
        FROM budget_allocations AS b
        WHERE b.user_id = $1
        ORDER BY b.is_active DESC, b.period_start_unix DESC, b.id DESC
        LIMIT $2
        "#,
        user_id,
        MAX_BUDGETS_PER_RESPONSE,
    )
    .fetch_all(pool)
    .await?;

    Ok(rows.into_iter().map(map_row).collect())
}

/// Creates an allocation and its default 80%/100% threshold rules atomically.
pub async fn create_budget(
    pool: &PgPool,
    user_id: i64,
    category: &str,
    amount_minor_units: i64,
    period_start_unix: i64,
    period_end_unix: Option<i64>,
    now_unix: i64,
) -> Result<BudgetRow, sqlx::Error> {
    let mut transaction = pool.begin().await?;

    let inserted = sqlx::query!(
        r#"
        INSERT INTO budget_allocations (
            user_id, category, amount_minor_units, period_start_unix,
            period_end_unix, is_active, created_at_unix, updated_at_unix, revision
        )
        VALUES ($1, $2, $3, $4, $5, TRUE, $6, $6, 1)
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

    for threshold_percent in [80_i16, 100_i16] {
        sqlx::query!(
            r#"
            INSERT INTO budget_threshold_rules (
                user_id, budget_id, threshold_percent, is_enabled,
                created_at_unix, updated_at_unix
            ) VALUES ($1, $2, $3, TRUE, $4, $4)
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

/// Updates only the matching owner's expected revision.
/// A stale revision and a foreign/missing ID both return `None`.
pub async fn update_budget(
    pool: &PgPool,
    user_id: i64,
    budget_id: i64,
    expected_revision: i64,
    category: &str,
    amount_minor_units: i64,
    period_start_unix: i64,
    period_end_unix: Option<i64>,
    now_unix: i64,
) -> Result<Option<BudgetRow>, sqlx::Error> {
    let mut transaction = pool.begin().await?;

    let updated = sqlx::query!(
        r#"
        UPDATE budget_allocations
        SET category = $1,
            amount_minor_units = $2,
            period_start_unix = $3,
            period_end_unix = $4,
            updated_at_unix = $5,
            revision = revision + 1
        WHERE id = $6 AND user_id = $7 AND revision = $8
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

    // Ensure a newly introduced budget can never become visible without its
    // default threshold rules, including after an older/partial migration.
    for threshold_percent in [80_i16, 100_i16] {
        sqlx::query!(
            r#"
            INSERT INTO budget_threshold_rules (
                user_id, budget_id, threshold_percent, is_enabled,
                created_at_unix, updated_at_unix
            ) VALUES ($1, $2, $3, TRUE, $4, $4)
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
    pool: &PgPool,
    user_id: i64,
    budget_id: i64,
    expected_revision: i64,
) -> Result<bool, sqlx::Error> {
    let row = sqlx::query!(
        r#"
        DELETE FROM budget_allocations
        WHERE id = $1 AND user_id = $2 AND revision = $3
        RETURNING id AS "id!"
        "#,
        budget_id,
        user_id,
        expected_revision,
    )
    .fetch_optional(pool)
    .await?;
    Ok(row.is_some())
}

async fn fetch_budget(
    transaction: &mut sqlx::Transaction<'_, sqlx::Postgres>,
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
            COALESCE((
                SELECT COALESCE(SUM(ROUND(ABS(t.price::numeric) * 100)), 0)::bigint
                FROM transactions AS t
                WHERE t.user_id = b.user_id
                  AND t.price < 0
                  AND t.datetime >= b.period_start_unix
                  AND (b.period_end_unix IS NULL OR t.datetime <= b.period_end_unix)
                  AND (b.category = 'Overall' OR t.category = b.category)
            ), 0)::bigint AS "spent_minor_units!",
            b.period_start_unix AS "period_start_unix!",
            b.period_end_unix AS "period_end_unix?",
            b.is_active AS "is_active!",
            b.revision AS "revision!"
        FROM budget_allocations AS b
        WHERE b.user_id = $1 AND b.id = $2
        "#,
        user_id,
        budget_id,
    )
    .fetch_one(&mut **transaction)
    .await?;
    Ok(map_row(row))
}
