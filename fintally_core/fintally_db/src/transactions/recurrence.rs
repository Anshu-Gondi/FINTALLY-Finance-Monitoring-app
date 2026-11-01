//! Bounded, transactional CloudSync recurring-transaction generation.
//!
//! Due templates are claimed with `FOR UPDATE SKIP LOCKED`. Every generated
//! occurrence and its template cursor update commit in the same transaction.

use crate::models::RecurringFrequency;
use fintally_recurrence::{
    add_months_from_anchor, next_occurrence_at, BatchStats, Frequency, ScheduleError,
};
use sqlx::PgPool;
use thiserror::Error;

const HARD_MAX_TEMPLATES: i64 = 16;
const HARD_MAX_CATCH_UP: u8 = 8;

#[derive(Debug, Error)]
pub enum RecurrenceRepositoryError {
    #[error("recurrence database operation failed")]
    Database(#[from] sqlx::Error),
    #[error("recurrence schedule timestamp is invalid")]
    Schedule(#[from] ScheduleError),
    #[error("stored recurring transaction has an unsupported frequency")]
    InvalidFrequency,
}

/// Processes one bounded PostgreSQL batch. Call repeatedly while the returned
/// `templates_scanned` equals the requested batch size, up to a caller limit.
pub async fn process_due_batch(
    pool: &PgPool,
    now_unix_seconds: i64,
    template_limit: i64,
    catch_up_per_template: u8,
    after_template_id: i64,
) -> Result<BatchStats, RecurrenceRepositoryError> {
    let template_limit = template_limit.clamp(1, HARD_MAX_TEMPLATES);
    let catch_up_per_template = catch_up_per_template.clamp(1, HARD_MAX_CATCH_UP);
    let mut transaction = pool.begin().await?;

    let rows = sqlx::query!(
        r#"
        SELECT
            id,
            user_id,
            name AS "name!",
            price AS "price!",
            description AS "description!",
            datetime AS "datetime!",
            category AS "category!",
            recurring_frequency::text AS "recurring_frequency!",
            last_generated_at::bigint AS "last_generated_at?",
            schedule_anchor_at::bigint AS "schedule_anchor_at?",
            next_run_at::bigint AS "next_run_at?",
            ends_at::bigint AS "ends_at?",
            tenure_months::bigint AS "tenure_months?",
            receipt_url AS "receipt_url?"
        FROM transactions
        WHERE is_recurring = TRUE
          AND recurring_frequency IS NOT NULL
          AND name IS NOT NULL AND octet_length(name) BETWEEN 1 AND 128
          AND description IS NOT NULL AND octet_length(description) <= 512
          AND category IS NOT NULL AND octet_length(category) BETWEEN 1 AND 64
          AND (receipt_url IS NULL OR octet_length(receipt_url) <= 512)
          AND (
              next_run_at IS NULL
              OR next_run_at <= $1
              OR (ends_at IS NOT NULL AND ends_at < $1)
              OR (ends_at IS NULL AND tenure_months <= 0)
          )
        ORDER BY CASE WHEN id > $3 THEN 0 ELSE 1 END,
                 COALESCE(next_run_at, last_generated_at, datetime), id
        LIMIT $2
        FOR UPDATE SKIP LOCKED
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

        // Legacy `tenure_months` was decremented once per occurrence, even for
        // daily/weekly schedules. Interpret it as a calendar duration instead.
        // This conversion is written atomically the first time the template is
        // visited; an explicit `ends_at` always takes precedence.
        let mut ends_at = row.ends_at;
        if ends_at.is_none() {
            if let Some(legacy_months) = row.tenure_months {
                if legacy_months <= 0 {
                    deactivate_template(
                        &mut transaction,
                        row.id,
                        row.user_id,
                        row.last_generated_at,
                        schedule_anchor_at,
                        row.ends_at,
                    )
                    .await?;
                    stats.schedules_completed = stats.schedules_completed.saturating_add(1);
                    continue;
                }
                ends_at = Some(add_months_from_anchor(schedule_anchor_at, legacy_months)?);
            }
        }

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
                VALUES ($1, $2, $3, $4, $5, $6, FALSE, NULL, $7, $8, $9)
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
            deactivate_template(
                &mut transaction,
                row.id,
                row.user_id,
                last_generated_at,
                schedule_anchor_at,
                ends_at,
            )
            .await?;
            stats.schedules_completed = stats.schedules_completed.saturating_add(1);
        } else {
            let next_frequency = Some(to_pg_frequency(frequency));
            sqlx::query!(
                r#"
                UPDATE transactions
                SET last_generated_at = $1,
                    schedule_anchor_at = $2,
                    next_run_at = $3,
                    ends_at = $4,
                    is_recurring = TRUE,
                    recurring_frequency = $5::recurring_frequency
                WHERE id = $6 AND user_id = $7
                "#,
                last_generated_at,
                schedule_anchor_at,
                next_run_at,
                ends_at,
                next_frequency as _,
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

async fn deactivate_template(
    transaction: &mut sqlx::Transaction<'_, sqlx::Postgres>,
    id: i64,
    user_id: i64,
    last_generated_at: Option<i64>,
    schedule_anchor_at: i64,
    ends_at: Option<i64>,
) -> Result<(), sqlx::Error> {
    sqlx::query!(
        r#"
        UPDATE transactions
        SET last_generated_at = $1,
            schedule_anchor_at = $2,
            next_run_at = NULL,
            ends_at = $3,
            is_recurring = FALSE,
            recurring_frequency = NULL
        WHERE id = $4 AND user_id = $5
        "#,
        last_generated_at,
        schedule_anchor_at,
        ends_at,
        id,
        user_id,
    )
    .execute(&mut **transaction)
    .await?;
    Ok(())
}

fn parse_frequency(value: &str) -> Result<Frequency, RecurrenceRepositoryError> {
    match value.to_ascii_lowercase().as_str() {
        "daily" => Ok(Frequency::Daily),
        "weekly" => Ok(Frequency::Weekly),
        "monthly" => Ok(Frequency::Monthly),
        _ => Err(RecurrenceRepositoryError::InvalidFrequency),
    }
}

fn to_pg_frequency(value: Frequency) -> RecurringFrequency {
    match value {
        Frequency::Daily => RecurringFrequency::Daily,
        Frequency::Weekly => RecurringFrequency::Weekly,
        Frequency::Monthly => RecurringFrequency::Monthly,
    }
}
