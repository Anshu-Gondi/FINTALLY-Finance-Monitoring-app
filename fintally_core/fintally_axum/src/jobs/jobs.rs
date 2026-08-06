use std::sync::Arc;
use std::time::Duration;

// Replace chrono with the time crate
use time::{Duration as TimeDuration, OffsetDateTime, Time};
use sqlx::{PgPool, Postgres, Transaction};
use tracing::{error, info, warn};

use fintally_chatbot::chatbot_service::rag_service::RagService;
use fintally_db::models::RecurringFrequency;

// ─────────────────────────────────────────────────────────────────────────────
// Period Boundary Check Helpers
// ─────────────────────────────────────────────────────────────────────────────

fn is_due(frequency: &RecurringFrequency, last_generated: OffsetDateTime, now: OffsetDateTime) -> bool {
    match frequency {
        RecurringFrequency::Daily => {
            now >= last_generated + TimeDuration::days(1)
        }
        RecurringFrequency::Weekly => {
            now >= last_generated + TimeDuration::weeks(1)
        }
        RecurringFrequency::Monthly => {
            now >= last_generated + TimeDuration::days(28)
                && (last_generated.year(), last_generated.month()) != (now.year(), now.month())
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Core Job Functions
// ─────────────────────────────────────────────────────────────────────────────

async fn generate_recurring_transactions(pool: &PgPool) -> Result<(), sqlx::Error> {
    let now_dt = OffsetDateTime::now_utc();
    let now_i64 = now_dt.unix_timestamp(); // Storing as i64 in the DB

    let active_txns = sqlx::query!(
        r#"
        SELECT
            id, user_id, name, price, description, datetime, category,
            recurring_frequency as "recurring_frequency: RecurringFrequency",
            last_generated_at, tenure_months, receipt_url
        FROM transactions
        WHERE is_recurring = true
          AND recurring_frequency IS NOT NULL
          AND (tenure_months > 0 OR tenure_months IS NULL)
        "#
    )
    .fetch_all(pool)
    .await?;

    if active_txns.is_empty() {
        return Ok(());
    }

    let mut tx: Transaction<'_, Postgres> = pool.begin().await?;
    let mut inserts_count = 0;

    for row in active_txns {
        let freq = match row.recurring_frequency {
            Some(f) => f,
            None => continue,
        };

        // DB returns i64 timestamps, fallback to transaction's datetime if last_generated_at is missing
        let last_generated_i64 = row.last_generated_at.unwrap_or(row.datetime);
        let last_generated_dt = OffsetDateTime::from_unix_timestamp(last_generated_i64)
            .unwrap_or(now_dt);

        if !is_due(&freq, last_generated_dt, now_dt) {
            continue;
        }

        // 1. Insert newly generated non-recurring transaction instance
        sqlx::query!(
            r#"
            INSERT INTO transactions (user_id, name, price, description, datetime, category, is_recurring, recurring_frequency, receipt_url)
            VALUES ($1, $2, $3, $4, $5, $6, false, null, $7)
            "#,
            row.user_id,
            row.name,
            row.price,
            row.description,
            now_i64, // Extracted i64
            row.category,
            row.receipt_url
        )
        .execute(&mut *tx)
        .await?;

        inserts_count += 1;

        let mut next_is_recurring = true;
        let mut next_freq = Some(freq);
        let next_tenure = row.tenure_months.map(|t| (t - 1).max(0));

        if let Some(0) = next_tenure {
            next_is_recurring = false;
            next_freq = None;
        }

        // 2. Update metadata on original template record
        sqlx::query!(
            r#"
            UPDATE transactions
            SET
                last_generated_at = $1,
                tenure_months = $2,
                is_recurring = $3,
                recurring_frequency = $4::recurring_frequency
            WHERE id = $5
            "#,
            now_i64, // Extracted i64
            next_tenure,
            next_is_recurring,
            next_freq as Option<RecurringFrequency>,
            row.id
        )
        .execute(&mut *tx)
        .await?;
    }

    if inserts_count > 0 {
        tx.commit().await?;
        info!("✅ {} recurring transactions generated successfully.", inserts_count);
    } else {
        let _ = tx.rollback().await;
    }

    Ok(())
}

async fn run_daily_rag_sync(rag_service: Arc<RagService>) {
    info!("[RAG-JOB] Triggering Google Drive ingestion synchronization...");
    if let Err(e) = rag_service.sync_and_reindex_trusted_sources().await {
        error!("❌ Error executing automated Google Drive synchronization: {:?}", e);
    } else {
        info!("✅ Refreshed native vector layer indexes.");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Engine Initializers
// ─────────────────────────────────────────────────────────────────────────────

pub fn start_scheduler(pool: PgPool, rag_service: Arc<RagService>) {
    // Worker 1: Recurring Transactions Execution (Runs immediately at startup, then every 5 mins)
    let pool_tx = pool.clone();
    tokio::spawn(async move {
        info!("🔄 Transaction Processing Scheduler Online. Running every 5 minutes.");
        let mut interval = tokio::time::interval(Duration::from_secs(5 * 60));

        loop {
            interval.tick().await; // Executes immediately on first loop pass
            if let Err(e) = generate_recurring_transactions(&pool_tx).await {
                error!("❌ Error in recurring transaction job: {:?}", e);
            }
        }
    });

    // Worker 2: Hybrid Initial & Daily RAG Ingestion Cron
    tokio::spawn(async move {
        info!("📅 RAG Context Automation Scheduler Online.");

        // ⚡ First-Time Startup Check: If indices are missing, generate them immediately!
        if !rag_service.has_indexes() {
            warn!("⚠️ RAG vector index or chunk DB not found on startup! Launching immediate initial indexing task...");
            run_daily_rag_sync(rag_service.clone()).await;
        } else {
            info!("✅ Existing RAG vector indexes found. Skipping initial startup build.");
        }

        // Continue with the standard daily scheduled loop (02:30 AM UTC)
        loop {
            let now = OffsetDateTime::now_utc();

            // Set target time to 02:30:00 UTC
            let target_time = Time::from_hms(2, 30, 0).unwrap();
            let mut target_datetime = now.replace_time(target_time);

            // If 2:30 AM has already passed today, target 2:30 AM tomorrow
            if now >= target_datetime {
                target_datetime += TimeDuration::days(1);
            }

            let diff = target_datetime - now;
            let duration_to_sleep = if diff.is_positive() {
                Duration::from_secs(diff.whole_seconds() as u64)
            } else {
                Duration::from_secs(60) // Fallback just in case
            };

            info!("[RAG-SCHEDULER] Next indexing sync scheduled in {:.2} hours.", duration_to_sleep.as_secs_f64() / 3600.0);
            tokio::time::sleep(duration_to_sleep).await;

            run_daily_rag_sync(rag_service.clone()).await;
        }
    });
}
