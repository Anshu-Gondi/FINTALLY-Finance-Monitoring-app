//! Bounded, transactional LocalOnly recurring-transaction generation.
//!
//! SQLite has one writer at a time. The first no-op UPDATE acquires the write
//! reservation before selecting templates; all occurrence inserts and cursor
//! updates then commit atomically in the same transaction.

use crate::LocalDbContext;
use kinetib_recurrence::{next_occurrence_at, BatchStats, Frequency, ScheduleError};
use thiserror::Error;

const HARD_MAX_TEMPLATES: i64 = 16;
const HARD_MAX_CATCH_UP: u8 = 8;

#[derive(Debug, Error)]
pub enum RecurrenceRepositoryError {
    #[error("local recurrence database operation failed")]
    Database(#[from] sqlx::Error),
    #[error("recurrence schedule timestamp is invalid")]
    Schedule(#[from] ScheduleError),
    #[error("stored recurring transaction has an unsupported frequency")]
    InvalidFrequency,
}

/// Processes one bounded SQLite batch. SQLite serializes the write transaction;
/// the occurrence unique index remains the final idempotency guard.
pub async fn process_due_batch(
    db: &LocalDbContext,
    now_unix_seconds: i64,
    template_limit: i64,
    catch_up_per_template: u8,
    after_template_id: i64,
) -> Result<BatchStats, RecurrenceRepositoryError> {
    let template_limit = template_limit.clamp(1, HARD_MAX_TEMPLATES);
    let catch_up_per_template = catch_up_per_template.clamp(1, HARD_MAX_CATCH_UP);
    let mut transaction = db.pool.begin().await?;

    // SQLite's default transaction is deferred. This no-op write reserves the
    // single-writer slot before the due-row read, preventing competing local
    // kernel workers from both claiming the same templates.
    sqlx::query!("UPDATE transactions SET id = id WHERE 0")
        .execute(&mut *transaction)
        .await?;

    let rows = sqlx::query!(
        r#"
        SELECT
            id AS "id!",
            user_id AS "user_id!",
            name AS "name!",
            price AS "price!: f64",
            description AS "description!",
            datetime AS "datetime!",
            category AS "category!",
            recurring_frequency AS "recurring_frequency!",
            last_generated_at AS "last_generated_at?: i64",
            schedule_anchor_at AS "schedule_anchor_at?: i64",
            next_run_at AS "next_run_at?: i64",
            ends_at AS "ends_at?: i64",
            receipt_url AS "receipt_url?: String"
        FROM transactions
        WHERE is_recurring = 1
          AND recurring_frequency IS NOT NULL
          AND name IS NOT NULL AND length(CAST(name AS BLOB)) BETWEEN 1 AND 128
          AND description IS NOT NULL AND length(CAST(description AS BLOB)) <= 512
          AND category IS NOT NULL AND length(CAST(category AS BLOB)) BETWEEN 1 AND 64
          AND (receipt_url IS NULL OR length(CAST(receipt_url AS BLOB)) <= 512)
          AND (
              next_run_at IS NULL
              OR next_run_at <= ?1
              OR (ends_at IS NOT NULL AND ends_at < ?1)
          )
        ORDER BY CASE WHEN id > ?3 THEN 0 ELSE 1 END,
                 COALESCE(next_run_at, last_generated_at, datetime), id
        LIMIT ?2
        "#,
        now_unix_seconds,
        template_limit,
        after_template_id,
    )
    .fetch_all(&mut *transaction)
    .await?;

    let mut stats = BatchStats {
        templates_scanned: u32::try_from(rows.len()).unwrap_or(u32::MAX),
        last_template_id: rows.last().map(|row| row.id),
        ..BatchStats::default()
    };

    for row in rows {
        let frequency = parse_frequency(&row.recurring_frequency)?;
        let schedule_anchor_at = row
            .schedule_anchor_at
            .or(row.last_generated_at)
            .unwrap_or(row.datetime);
        let ends_at = row.ends_at;
        let previous_occurrence = row.last_generated_at.unwrap_or(schedule_anchor_at);
        let mut next_run_at = match row.next_run_at {
            Some(value) => value,
            None => next_occurrence_at(schedule_anchor_at, previous_occurrence, frequency)?,
        };
        let mut last_generated_at = row.last_generated_at;
        let mut generated_for_template = 0_u8;
        let mut completed = false;

        loop {
            if ends_at.is_some_and(|end| next_run_at > end) {
                completed = true;
                break;
            }
            if next_run_at > now_unix_seconds || generated_for_template >= catch_up_per_template {
                break;
            }

            let inserted = sqlx::query!(
                r#"
                INSERT INTO transactions (
                    user_id, name, price, description, datetime, category,
                    is_recurring, recurring_frequency, receipt_url,
                    template_id, scheduled_for
                )
                VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, NULL, ?7, ?8, ?9)
                ON CONFLICT (template_id, scheduled_for)
                    WHERE template_id IS NOT NULL AND scheduled_for IS NOT NULL
                DO NOTHING
                "#,
                row.user_id,
                row.name,
                row.price,
                row.description,
                next_run_at,
                row.category,
                row.receipt_url,
                row.id,
                next_run_at,
            )
            .execute(&mut *transaction)
            .await?;

            if inserted.rows_affected() > 0 {
                stats.occurrences_created = stats.occurrences_created.saturating_add(1);
            }
            generated_for_template = generated_for_template.saturating_add(1);
            last_generated_at = Some(next_run_at);
            next_run_at = next_occurrence_at(schedule_anchor_at, next_run_at, frequency)?;
        }

        if ends_at.is_some_and(|end| next_run_at > end) {
            completed = true;
        }

        if completed {
            sqlx::query!(
                r#"
                UPDATE transactions
                SET last_generated_at = ?1,
                    schedule_anchor_at = ?2,
                    next_run_at = NULL,
                    ends_at = ?3,
                    is_recurring = 0,
                    recurring_frequency = NULL
                WHERE id = ?4 AND user_id = ?5
                "#,
                last_generated_at,
                schedule_anchor_at,
                ends_at,
                row.id,
                row.user_id,
            )
            .execute(&mut *transaction)
            .await?;
            stats.schedules_completed = stats.schedules_completed.saturating_add(1);
        } else {
            let next_frequency = Some(frequency_as_str(frequency));
            sqlx::query!(
                r#"
                UPDATE transactions
                SET last_generated_at = ?1,
                    schedule_anchor_at = ?2,
                    next_run_at = ?3,
                    ends_at = ?4,
                    is_recurring = 1,
                    recurring_frequency = ?5
                WHERE id = ?6 AND user_id = ?7
                "#,
                last_generated_at,
                schedule_anchor_at,
                next_run_at,
                ends_at,
                next_frequency,
                row.id,
                row.user_id,
            )
            .execute(&mut *transaction)
            .await?;
        }
    }

    transaction.commit().await?;
    Ok(stats)
}

fn parse_frequency(value: &str) -> Result<Frequency, RecurrenceRepositoryError> {
    match value.to_ascii_lowercase().as_str() {
        "daily" => Ok(Frequency::Daily),
        "weekly" => Ok(Frequency::Weekly),
        "monthly" => Ok(Frequency::Monthly),
        _ => Err(RecurrenceRepositoryError::InvalidFrequency),
    }
}

fn frequency_as_str(value: Frequency) -> &'static str {
    match value {
        Frequency::Daily => "daily",
        Frequency::Weekly => "weekly",
        Frequency::Monthly => "monthly",
    }
}

#[cfg(test)]
mod tests {
    use super::process_due_batch;
    use crate::transactions::{create_transaction, list_transactions, NewTransaction};
    use crate::LocalDbContext;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_path() -> std::path::PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system clock after epoch")
            .as_nanos();
        std::env::temp_dir().join(format!("kinetib-local-recurrence-{nonce}.sqlite3"))
    }

    #[tokio::test]
    async fn daily_catch_up_is_bounded_and_retry_is_idempotent() {
        let path = temp_path();
        let db = LocalDbContext::connect(&path)
            .await
            .expect("connect local recurrence database");
        let user_id = db
            .create_user("recurrence@example.test", "hash", 1_800_000_000)
            .await
            .expect("create local user");
        let anchor = 1_800_000_000_i64;
        create_transaction(
            &db,
            user_id,
            NewTransaction {
                name: "Daily rent reserve".into(),
                price: 100.0,
                description: "Recurring test template".into(),
                datetime_unix_seconds: anchor,
                category: "housing".into(),
                is_recurring: true,
                recurring_frequency: Some("daily".into()),
            },
        )
        .await
        .expect("create recurring template");

        let now = anchor + 5 * 86_400;
        let first = process_due_batch(&db, now, 32, 3, 0)
            .await
            .expect("first bounded catch-up batch");
        assert_eq!(first.templates_scanned, 1);
        assert_eq!(first.occurrences_created, 3);
        assert!(first.last_template_id.is_some());

        let second = process_due_batch(&db, now, 32, 3, first.last_template_id.unwrap_or_default())
            .await
            .expect("second bounded catch-up batch");
        assert_eq!(second.occurrences_created, 2);

        let retry = process_due_batch(&db, now, 32, 3, 0)
            .await
            .expect("idempotent retry");
        assert_eq!(retry.occurrences_created, 0);

        let rows = list_transactions(&db, user_id, 16, 0)
            .await
            .expect("list template and generated instances");
        assert_eq!(rows.len(), 6);
        assert_eq!(rows.iter().filter(|row| row.is_recurring).count(), 1);
        assert_eq!(rows.iter().filter(|row| !row.is_recurring).count(), 5);
        let mut generated_times = rows
            .iter()
            .filter(|row| !row.is_recurring)
            .map(|row| row.datetime_unix_seconds)
            .collect::<Vec<_>>();
        generated_times.sort_unstable();
        assert_eq!(
            generated_times,
            (1_i64..=5)
                .map(|day| anchor + day * 86_400)
                .collect::<Vec<_>>()
        );

        drop(db);
        let _ = std::fs::remove_file(&path);
        let _ = std::fs::remove_file(path.with_extension("sqlite3-wal"));
        let _ = std::fs::remove_file(path.with_extension("sqlite3-shm"));
    }
}
