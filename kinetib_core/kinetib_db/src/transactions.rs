//! Compile-time checked PostgreSQL transaction repository.
//!
//! This module intentionally contains SQLx query macros rather than dynamic
//! `query()` calls. It is scoped to the CloudSync identity selected by the
//! kernel; callers must pass the user ID derived from an authenticated session.

pub mod recurrence;

use crate::models::RecurringFrequency;
use sqlx::PgPool;

// Text lengths are constrained in the SQL projection literals below. Keep those
// limits synchronized with the IPC protocol field bounds.

#[derive(Debug, Clone)]
pub struct TransactionRow {
    pub id: i64,
    pub name: Option<String>,
    pub name_within_limit: bool,
    pub price: f64,
    pub description: Option<String>,
    pub description_within_limit: bool,
    pub datetime_unix_seconds: i64,
    pub category: Option<String>,
    pub category_within_limit: bool,
    pub is_recurring: bool,
    pub recurring_frequency: Option<String>,
    pub receipt_url: Option<String>,
    pub receipt_url_within_limit: bool,
}

#[derive(Debug, Clone)]
pub struct NewTransaction {
    pub name: String,
    pub price: f64,
    pub description: String,
    pub datetime_unix_seconds: i64,
    pub category: String,
    pub is_recurring: bool,
    pub recurring_frequency: Option<RecurringFrequency>,
}

/// Changed flags preserve the distinction between an omitted field and an
/// explicit clear for `recurring_frequency`, which is nullable in PostgreSQL.
#[derive(Debug, Clone, Default)]
pub struct TransactionChanges {
    pub name_changed: bool,
    pub name: Option<String>,
    pub price_changed: bool,
    pub price: Option<f64>,
    pub description_changed: bool,
    pub description: Option<String>,
    pub datetime_changed: bool,
    pub datetime_unix_seconds: Option<i64>,
    pub category_changed: bool,
    pub category: Option<String>,
    pub is_recurring_changed: bool,
    pub is_recurring: Option<bool>,
    pub recurring_frequency_changed: bool,
    pub recurring_frequency: Option<RecurringFrequency>,
}

#[derive(Debug, Clone)]
pub struct ReceiptRow {
    pub transaction_id: i64,
    pub name: Option<String>,
    pub name_within_limit: bool,
    pub price: f64,
    pub description: Option<String>,
    pub description_within_limit: bool,
    pub datetime_unix_seconds: i64,
    pub category: Option<String>,
    pub category_within_limit: bool,
}

/// Insert a validated transaction and return its canonical database record.
pub async fn create_transaction(
    pool: &PgPool,
    user_id: i64,
    transaction: NewTransaction,
) -> Result<TransactionRow, sqlx::Error> {
    let row = sqlx::query!(
        r#"
        INSERT INTO transactions
            (user_id, name, price, description, datetime, category, is_recurring,
             recurring_frequency, receipt_url)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8::recurring_frequency, NULL)
        RETURNING
            id,
            CASE WHEN octet_length(name) <= 128 THEN name ELSE NULL END AS "name?",
            (octet_length(name) <= 128) AS "name_within_limit!",
            price,
            CASE WHEN octet_length(description) <= 512 THEN description ELSE NULL END AS "description?",
            (octet_length(description) <= 512) AS "description_within_limit!",
            datetime AS datetime_unix_seconds,
            CASE WHEN octet_length(category) <= 64 THEN category ELSE NULL END AS "category?",
            (octet_length(category) <= 64) AS "category_within_limit!",
            is_recurring,
            recurring_frequency::text AS "recurring_frequency?",
            CASE WHEN receipt_url IS NULL OR octet_length(receipt_url) <= 512 THEN receipt_url ELSE NULL END AS "receipt_url?",
            (receipt_url IS NULL OR octet_length(receipt_url) <= 512) AS "receipt_url_within_limit!"
        "#,
        user_id,
        transaction.name,
        transaction.price,
        transaction.description,
        transaction.datetime_unix_seconds,
        transaction.category,
        transaction.is_recurring,
        transaction.recurring_frequency as _,
    )
    .fetch_one(pool)
    .await?;

    Ok(TransactionRow {
        id: row.id,
        name: row.name,
        name_within_limit: row.name_within_limit,
        price: row.price,
        description: row.description,
        description_within_limit: row.description_within_limit,
        datetime_unix_seconds: row.datetime_unix_seconds,
        category: row.category,
        category_within_limit: row.category_within_limit,
        is_recurring: row.is_recurring,
        recurring_frequency: row.recurring_frequency,
        receipt_url: row.receipt_url,
        receipt_url_within_limit: row.receipt_url_within_limit,
    })
}

/// Lists a stable page. The owner predicate is mandatory and `id DESC` breaks
/// timestamp ties so rows do not shuffle between adjacent pages.
pub async fn list_transactions(
    pool: &PgPool,
    user_id: i64,
    limit: i64,
    offset: i64,
) -> Result<Vec<TransactionRow>, sqlx::Error> {
    let rows = sqlx::query!(
        r#"
        SELECT
            id,
            CASE WHEN COALESCE(octet_length(name) <= 128, FALSE) THEN name ELSE NULL END AS "name?",
            COALESCE(octet_length(name) <= 128, FALSE) AS "name_within_limit!",
            price,
            CASE WHEN COALESCE(octet_length(description) <= 512, FALSE) THEN description ELSE NULL END AS "description?",
            COALESCE(octet_length(description) <= 512, FALSE) AS "description_within_limit!",
            datetime AS datetime_unix_seconds,
            CASE WHEN COALESCE(octet_length(category) <= 64, FALSE) THEN category ELSE NULL END AS "category?",
            COALESCE(octet_length(category) <= 64, FALSE) AS "category_within_limit!",
            is_recurring,
            recurring_frequency::text AS "recurring_frequency?",
            CASE WHEN receipt_url IS NULL OR octet_length(receipt_url) <= 512 THEN receipt_url ELSE NULL END AS "receipt_url?",
            COALESCE(receipt_url IS NULL OR octet_length(receipt_url) <= 512, FALSE) AS "receipt_url_within_limit!"
        FROM transactions
        WHERE user_id = $1
        ORDER BY datetime DESC, id DESC
        LIMIT $2 OFFSET $3
        "#,
        user_id,
        limit,
        offset,
    )
    .fetch_all(pool)
    .await?;

    Ok(rows
        .into_iter()
        .map(|row| TransactionRow {
            id: row.id,
            name: row.name,
            name_within_limit: row.name_within_limit,
            price: row.price,
            description: row.description,
            description_within_limit: row.description_within_limit,
            datetime_unix_seconds: row.datetime_unix_seconds,
            category: row.category,
            category_within_limit: row.category_within_limit,
            is_recurring: row.is_recurring,
            recurring_frequency: row.recurring_frequency,
            receipt_url: row.receipt_url,
            receipt_url_within_limit: row.receipt_url_within_limit,
        })
        .collect())
}

/// Updates fields only when their corresponding changed flag is true. The row
/// and owner predicates are combined into one statement to avoid a check/write
/// race and to distinguish a missing or foreign-owned transaction.
pub async fn update_transaction(
    pool: &PgPool,
    user_id: i64,
    transaction_id: i64,
    changes: TransactionChanges,
) -> Result<Option<TransactionRow>, sqlx::Error> {
    let row = sqlx::query!(
        r#"
        UPDATE transactions
        SET name = CASE WHEN $1 THEN $2 ELSE name END,
            price = CASE WHEN $3 THEN $4 ELSE price END,
            description = CASE WHEN $5 THEN $6 ELSE description END,
            datetime = CASE WHEN $7 THEN $8 ELSE datetime END,
            category = CASE WHEN $9 THEN $10 ELSE category END,
            is_recurring = CASE WHEN $11 THEN $12 ELSE is_recurring END,
            recurring_frequency = CASE WHEN $13 THEN $14::recurring_frequency ELSE recurring_frequency END,
            schedule_anchor_at = CASE
                WHEN ($7 AND $8 IS DISTINCT FROM datetime) THEN $8
                WHEN ($11 AND $12 IS DISTINCT FROM is_recurring) THEN datetime
                WHEN ($13 AND $14::recurring_frequency IS DISTINCT FROM recurring_frequency)
                    THEN COALESCE(schedule_anchor_at, datetime)
                ELSE schedule_anchor_at
            END,
            last_generated_at = CASE
                WHEN (($7 AND $8 IS DISTINCT FROM datetime)
                   OR ($11 AND $12 IS DISTINCT FROM is_recurring)
                   OR ($13 AND $14::recurring_frequency IS DISTINCT FROM recurring_frequency))
                THEN NULL ELSE last_generated_at
            END,
            next_run_at = CASE
                WHEN (($7 AND $8 IS DISTINCT FROM datetime)
                   OR ($11 AND $12 IS DISTINCT FROM is_recurring)
                   OR ($13 AND $14::recurring_frequency IS DISTINCT FROM recurring_frequency))
                THEN NULL ELSE next_run_at
            END
        WHERE id = $15 AND user_id = $16
        RETURNING
            id,
            CASE WHEN octet_length(name) <= 128 THEN name ELSE NULL END AS "name?",
            (octet_length(name) <= 128) AS "name_within_limit!",
            price,
            CASE WHEN octet_length(description) <= 512 THEN description ELSE NULL END AS "description?",
            (octet_length(description) <= 512) AS "description_within_limit!",
            datetime AS datetime_unix_seconds,
            CASE WHEN octet_length(category) <= 64 THEN category ELSE NULL END AS "category?",
            (octet_length(category) <= 64) AS "category_within_limit!",
            is_recurring,
            recurring_frequency::text AS "recurring_frequency?",
            CASE WHEN receipt_url IS NULL OR octet_length(receipt_url) <= 512 THEN receipt_url ELSE NULL END AS "receipt_url?",
            (receipt_url IS NULL OR octet_length(receipt_url) <= 512) AS "receipt_url_within_limit!"
        "#,
        changes.name_changed,
        changes.name,
        changes.price_changed,
        changes.price,
        changes.description_changed,
        changes.description,
        changes.datetime_changed,
        changes.datetime_unix_seconds,
        changes.category_changed,
        changes.category,
        changes.is_recurring_changed,
        changes.is_recurring,
        changes.recurring_frequency_changed,
        changes.recurring_frequency as _,
        transaction_id,
        user_id,
    )
    .fetch_optional(pool)
    .await?;

    Ok(row.map(|row| TransactionRow {
        id: row.id,
        name: row.name,
        name_within_limit: row.name_within_limit,
        price: row.price,
        description: row.description,
        description_within_limit: row.description_within_limit,
        datetime_unix_seconds: row.datetime_unix_seconds,
        category: row.category,
        category_within_limit: row.category_within_limit,
        is_recurring: row.is_recurring,
        recurring_frequency: row.recurring_frequency,
        receipt_url: row.receipt_url,
        receipt_url_within_limit: row.receipt_url_within_limit,
    }))
}

/// Deletes only a row owned by `user_id` and returns the former receipt URL for
/// post-commit cleanup by a storage adapter. A missing row returns `None`.
pub async fn delete_transaction(
    pool: &PgPool,
    user_id: i64,
    transaction_id: i64,
) -> Result<Option<Option<String>>, sqlx::Error> {
    let row = sqlx::query!(
        "DELETE FROM transactions WHERE id = $1 AND user_id = $2 RETURNING receipt_url",
        transaction_id,
        user_id,
    )
    .fetch_optional(pool)
    .await?;
    Ok(row.map(|row| row.receipt_url))
}

/// Returns bounded receipt-rendering metadata for the authenticated owner.
pub async fn get_receipt_data(
    pool: &PgPool,
    user_id: i64,
    transaction_id: i64,
) -> Result<Option<ReceiptRow>, sqlx::Error> {
    let row = sqlx::query!(
        r#"
        SELECT
            id AS transaction_id,
            CASE WHEN octet_length(name) <= 128 THEN name ELSE NULL END AS "name?",
            (octet_length(name) <= 128) AS "name_within_limit!",
            price,
            CASE WHEN octet_length(description) <= 512 THEN description ELSE NULL END AS "description?",
            (octet_length(description) <= 512) AS "description_within_limit!",
            datetime AS datetime_unix_seconds,
            CASE WHEN octet_length(category) <= 64 THEN category ELSE NULL END AS "category?",
            (octet_length(category) <= 64) AS "category_within_limit!"
        FROM transactions
        WHERE id = $1 AND user_id = $2
        "#,
        transaction_id,
        user_id,
    )
    .fetch_optional(pool)
    .await?;

    Ok(row.map(|row| ReceiptRow {
        transaction_id: row.transaction_id,
        name: row.name,
        name_within_limit: row.name_within_limit,
        price: row.price,
        description: row.description,
        description_within_limit: row.description_within_limit,
        datetime_unix_seconds: row.datetime_unix_seconds,
        category: row.category,
        category_within_limit: row.category_within_limit,
    }))
}
