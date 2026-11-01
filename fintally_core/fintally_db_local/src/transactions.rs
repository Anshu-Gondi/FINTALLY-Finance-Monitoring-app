//! Compile-time checked SQLite transaction repository for LocalOnly sessions.
//!
//! This module is completely independent of the PostgreSQL crate. Every query
//! is scoped to `local_users.id`, and the transaction table is created by the
//! local migration rather than assumed to exist.

pub mod recurrence;

use crate::LocalDbContext;
use sqlx::Error;

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
    pub recurring_frequency: Option<String>,
}

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
    pub recurring_frequency: Option<String>,
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

pub async fn create_transaction(
    db: &LocalDbContext,
    user_id: i64,
    transaction: NewTransaction,
) -> Result<TransactionRow, Error> {
    let row = sqlx::query!(
        r#"
        INSERT INTO transactions
            (user_id, name, price, description, datetime, category, is_recurring,
             recurring_frequency, receipt_url)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, NULL)
        RETURNING
            id AS "id!",
            CASE WHEN length(CAST(name AS BLOB)) <= 128 THEN name ELSE NULL END AS "name?: String",
            (length(CAST(name AS BLOB)) <= 128) AS "name_within_limit!: bool",
            price AS "price!",
            CASE WHEN length(CAST(description AS BLOB)) <= 512 THEN description ELSE NULL END AS "description?: String",
            (length(CAST(description AS BLOB)) <= 512) AS "description_within_limit!: bool",
            datetime AS "datetime_unix_seconds!",
            CASE WHEN length(CAST(category AS BLOB)) <= 64 THEN category ELSE NULL END AS "category?: String",
            (length(CAST(category AS BLOB)) <= 64) AS "category_within_limit!: bool",
            is_recurring AS "is_recurring!: bool",
            recurring_frequency AS "recurring_frequency?: String",
            CASE WHEN receipt_url IS NULL OR length(CAST(receipt_url AS BLOB)) <= 512 THEN receipt_url ELSE NULL END AS "receipt_url?: String",
            (receipt_url IS NULL OR length(CAST(receipt_url AS BLOB)) <= 512) AS "receipt_url_within_limit!: bool"
        "#,
        user_id,
        transaction.name,
        transaction.price,
        transaction.description,
        transaction.datetime_unix_seconds,
        transaction.category,
        transaction.is_recurring,
        transaction.recurring_frequency,
    )
    .fetch_one(&db.pool)
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

pub async fn list_transactions(
    db: &LocalDbContext,
    user_id: i64,
    limit: i64,
    offset: i64,
) -> Result<Vec<TransactionRow>, Error> {
    let rows = sqlx::query!(
        r#"
        SELECT
            id AS "id!",
            CASE WHEN COALESCE(length(CAST(name AS BLOB)) <= 128, 0) THEN name ELSE NULL END AS "name?: String",
            COALESCE(length(CAST(name AS BLOB)) <= 128, 0) AS "name_within_limit!: bool",
            price AS "price!",
            CASE WHEN COALESCE(length(CAST(description AS BLOB)) <= 512, 0) THEN description ELSE NULL END AS "description?: String",
            COALESCE(length(CAST(description AS BLOB)) <= 512, 0) AS "description_within_limit!: bool",
            datetime AS "datetime_unix_seconds!",
            CASE WHEN COALESCE(length(CAST(category AS BLOB)) <= 64, 0) THEN category ELSE NULL END AS "category?: String",
            COALESCE(length(CAST(category AS BLOB)) <= 64, 0) AS "category_within_limit!: bool",
            is_recurring AS "is_recurring!: bool",
            recurring_frequency AS "recurring_frequency?: String",
            CASE WHEN receipt_url IS NULL OR length(CAST(receipt_url AS BLOB)) <= 512 THEN receipt_url ELSE NULL END AS "receipt_url?: String",
            COALESCE(receipt_url IS NULL OR length(CAST(receipt_url AS BLOB)) <= 512, 0) AS "receipt_url_within_limit!: bool"
        FROM transactions
        WHERE user_id = ?1
        ORDER BY datetime DESC, id DESC
        LIMIT ?2 OFFSET ?3
        "#,
        user_id,
        limit,
        offset,
    )
    .fetch_all(&db.pool)
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

pub async fn update_transaction(
    db: &LocalDbContext,
    user_id: i64,
    transaction_id: i64,
    changes: TransactionChanges,
) -> Result<Option<TransactionRow>, Error> {
    let name_changed = sqlite_flag(changes.name_changed);
    let price_changed = sqlite_flag(changes.price_changed);
    let description_changed = sqlite_flag(changes.description_changed);
    let datetime_changed = sqlite_flag(changes.datetime_changed);
    let category_changed = sqlite_flag(changes.category_changed);
    let is_recurring_changed = sqlite_flag(changes.is_recurring_changed);
    let is_recurring_value = changes.is_recurring.map(sqlite_flag);
    let recurring_frequency_changed = sqlite_flag(changes.recurring_frequency_changed);

    let row = sqlx::query!(
        r#"
        UPDATE transactions
        SET name = CASE WHEN CAST(?1 AS INTEGER) <> 0 THEN CAST(?2 AS TEXT) ELSE name END,
            price = CASE WHEN CAST(?3 AS INTEGER) <> 0 THEN CAST(?4 AS REAL) ELSE price END,
            description = CASE WHEN CAST(?5 AS INTEGER) <> 0 THEN CAST(?6 AS TEXT) ELSE description END,
            datetime = CASE WHEN CAST(?7 AS INTEGER) <> 0 THEN CAST(?8 AS INTEGER) ELSE datetime END,
            category = CASE WHEN CAST(?9 AS INTEGER) <> 0 THEN CAST(?10 AS TEXT) ELSE category END,
            is_recurring = CASE WHEN CAST(?11 AS INTEGER) <> 0 THEN CAST(?12 AS INTEGER) ELSE is_recurring END,
            recurring_frequency = CASE WHEN CAST(?13 AS INTEGER) <> 0 THEN CAST(?14 AS TEXT) ELSE recurring_frequency END,
            schedule_anchor_at = CASE
                WHEN (CAST(?7 AS INTEGER) <> 0 AND CAST(?8 AS INTEGER) IS NOT datetime)
                    THEN CAST(?8 AS INTEGER)
                WHEN (CAST(?11 AS INTEGER) <> 0 AND CAST(?12 AS INTEGER) IS NOT is_recurring)
                    THEN datetime
                WHEN (CAST(?13 AS INTEGER) <> 0 AND CAST(?14 AS TEXT) IS NOT recurring_frequency)
                    THEN COALESCE(schedule_anchor_at, datetime)
                ELSE schedule_anchor_at
            END,
            last_generated_at = CASE
                WHEN ((CAST(?7 AS INTEGER) <> 0 AND CAST(?8 AS INTEGER) IS NOT datetime)
                   OR (CAST(?11 AS INTEGER) <> 0 AND CAST(?12 AS INTEGER) IS NOT is_recurring)
                   OR (CAST(?13 AS INTEGER) <> 0 AND CAST(?14 AS TEXT) IS NOT recurring_frequency)) THEN NULL ELSE last_generated_at
            END,
            next_run_at = CASE
                WHEN ((CAST(?7 AS INTEGER) <> 0 AND CAST(?8 AS INTEGER) IS NOT datetime)
                   OR (CAST(?11 AS INTEGER) <> 0 AND CAST(?12 AS INTEGER) IS NOT is_recurring)
                   OR (CAST(?13 AS INTEGER) <> 0 AND CAST(?14 AS TEXT) IS NOT recurring_frequency)) THEN NULL ELSE next_run_at
            END
        WHERE id = CAST(?15 AS INTEGER) AND user_id = CAST(?16 AS INTEGER)
        RETURNING
            id AS "id!",
            CASE WHEN length(CAST(name AS BLOB)) <= 128 THEN name ELSE NULL END AS "name?: String",
            (length(CAST(name AS BLOB)) <= 128) AS "name_within_limit!: bool",
            price AS "price!",
            CASE WHEN length(CAST(description AS BLOB)) <= 512 THEN description ELSE NULL END AS "description?: String",
            (length(CAST(description AS BLOB)) <= 512) AS "description_within_limit!: bool",
            datetime AS "datetime_unix_seconds!",
            CASE WHEN length(CAST(category AS BLOB)) <= 64 THEN category ELSE NULL END AS "category?: String",
            (length(CAST(category AS BLOB)) <= 64) AS "category_within_limit!: bool",
            is_recurring AS "is_recurring!: bool",
            recurring_frequency AS "recurring_frequency?: String",
            CASE WHEN receipt_url IS NULL OR length(CAST(receipt_url AS BLOB)) <= 512 THEN receipt_url ELSE NULL END AS "receipt_url?: String",
            (receipt_url IS NULL OR length(CAST(receipt_url AS BLOB)) <= 512) AS "receipt_url_within_limit!: bool"
        "#,
        name_changed,
        changes.name,
        price_changed,
        changes.price,
        description_changed,
        changes.description,
        datetime_changed,
        changes.datetime_unix_seconds,
        category_changed,
        changes.category,
        is_recurring_changed,
        is_recurring_value,
        recurring_frequency_changed,
        changes.recurring_frequency,
        transaction_id,
        user_id,
    )
    .fetch_optional(&db.pool)
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

pub async fn delete_transaction(
    db: &LocalDbContext,
    user_id: i64,
    transaction_id: i64,
) -> Result<Option<Option<String>>, Error> {
    let row = sqlx::query!(
        "DELETE FROM transactions WHERE id = ?1 AND user_id = ?2 RETURNING receipt_url",
        transaction_id,
        user_id,
    )
    .fetch_optional(&db.pool)
    .await?;
    Ok(row.map(|row| row.receipt_url))
}

pub async fn get_receipt_data(
    db: &LocalDbContext,
    user_id: i64,
    transaction_id: i64,
) -> Result<Option<ReceiptRow>, Error> {
    let row = sqlx::query!(
        r#"
        SELECT
            id AS "transaction_id!",
            CASE WHEN length(CAST(name AS BLOB)) <= 128 THEN name ELSE NULL END AS "name?: String",
            (length(CAST(name AS BLOB)) <= 128) AS "name_within_limit!: bool",
            price AS "price!",
            CASE WHEN length(CAST(description AS BLOB)) <= 512 THEN description ELSE NULL END AS "description?: String",
            (length(CAST(description AS BLOB)) <= 512) AS "description_within_limit!: bool",
            datetime AS "datetime_unix_seconds!",
            CASE WHEN length(CAST(category AS BLOB)) <= 64 THEN category ELSE NULL END AS "category?: String",
            (length(CAST(category AS BLOB)) <= 64) AS "category_within_limit!: bool"
        FROM transactions WHERE id = ?1 AND user_id = ?2
        "#,
        transaction_id,
        user_id,
    )
    .fetch_optional(&db.pool)
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

/// Bind SQLite boolean change flags as explicit integers so the query macro
/// sees the same concrete storage type used by this schema.
#[inline]
fn sqlite_flag(value: bool) -> i64 {
    if value {
        1
    } else {
        0
    }
}

#[cfg(test)]
mod tests {
    use super::{
        create_transaction, delete_transaction, get_receipt_data, list_transactions,
        update_transaction, NewTransaction, TransactionChanges,
    };
    use crate::LocalDbContext;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_path() -> std::path::PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        std::env::temp_dir().join(format!("fintally-local-transactions-{nonce}.sqlite3"))
    }

    #[tokio::test]
    async fn local_transaction_crud_is_owner_scoped_and_supports_receipt_metadata() {
        let path = temp_path();
        let db = LocalDbContext::connect(&path)
            .await
            .expect("connect local store");
        let owner = db
            .create_user("owner@example.test", "hash", 1_800_000_000)
            .await
            .unwrap();
        let other = db
            .create_user("other@example.test", "hash", 1_800_000_000)
            .await
            .unwrap();
        let created = create_transaction(
            &db,
            owner,
            NewTransaction {
                name: "Groceries".into(),
                price: 25.5,
                description: "Weekly shop".into(),
                datetime_unix_seconds: 1_800_000_001,
                category: "food".into(),
                is_recurring: true,
                recurring_frequency: Some("weekly".into()),
            },
        )
        .await
        .unwrap();
        assert_eq!(created.name.as_deref(), Some("Groceries"));
        assert_eq!(created.recurring_frequency.as_deref(), Some("weekly"));

        let page = list_transactions(&db, owner, 10, 0).await.unwrap();
        assert_eq!(page.len(), 1);
        assert_eq!(page[0].id, created.id);
        assert!(list_transactions(&db, other, 10, 0)
            .await
            .unwrap()
            .is_empty());

        let updated = update_transaction(
            &db,
            owner,
            created.id,
            TransactionChanges {
                category_changed: true,
                category: Some("household".into()),
                is_recurring_changed: true,
                is_recurring: Some(false),
                recurring_frequency_changed: true,
                recurring_frequency: None,
                ..TransactionChanges::default()
            },
        )
        .await
        .unwrap()
        .unwrap();
        assert_eq!(updated.category.as_deref(), Some("household"));
        assert!(!updated.is_recurring);
        assert_eq!(updated.recurring_frequency, None);

        assert!(get_receipt_data(&db, other, created.id)
            .await
            .unwrap()
            .is_none());
        assert!(get_receipt_data(&db, owner, created.id)
            .await
            .unwrap()
            .is_some());
        assert!(delete_transaction(&db, other, created.id)
            .await
            .unwrap()
            .is_none());
        assert!(delete_transaction(&db, owner, created.id)
            .await
            .unwrap()
            .is_some());
        assert!(list_transactions(&db, owner, 10, 0)
            .await
            .unwrap()
            .is_empty());

        drop(db);
        let _ = std::fs::remove_file(&path);
        let _ = std::fs::remove_file(path.with_extension("sqlite3-wal"));
        let _ = std::fs::remove_file(path.with_extension("sqlite3-shm"));
    }
}
