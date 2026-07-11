use std::sync::Arc;
use std::time::Duration;
use chrono::{DateTime, Datelike, Utc, Timelike};
use sqlx::{PgPool, Postgres, Transaction};
use tracing::{info, error};

use fintally_chatbot::chatbot_service::rag_service::RagService; 
use fintally_db::models::RecurringFrequency;

// ─────────────────────────────────────────────────────────────────────────────
// Period Boundary Check Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Determines if a new financial window has elapsed since the last generation sequence
fn is_new_period(frequency: &RecurringFrequency, last_generated: DateTime<Utc>, now: DateTime<Utc>) -> bool {
    match frequency {
        RecurringFrequency::Daily => {
            last_generated.date_naive() != now.date_naive()
        }
        RecurringFrequency::Weekly => {
            let last_week = last_generated.iso_week();
            let now_week = now.iso_week();
            (last_week.year(), last_week.week()) != (now_week.year(), now_week.week())
        }
        RecurringFrequency::Monthly => {
            (last_generated.year(), last_generated.month()) != (now.year(), now.month())
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Core Job Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Mirrors the Python/Node recurring transaction generator engine using atomic PostgreSQL transitions
async fn generate_recurring_transactions(pool: &PgPool) -> Result<(), sqlx::Error> {
    let now = Utc::now();

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

        let last_generated = row.last_generated_at.unwrap_or(row.datetime);

        if !is_new_period(&freq, last_generated, now) {
            continue;
        }

        sqlx::query!(
            r#"
            INSERT INTO transactions (user_id, name, price, description, datetime, category, is_recurring, recurring_frequency, receipt_url)
            VALUES ($1, $2, $3, $4, $5, $6, false, null, $7)
            "#,
            row.user_id,
            row.name,
            row.price,
            row.description,
            now,
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
            now,
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
        info!("✅ {} recurring transaction instances processed and created successfully.", inserts_count);
    } else {
        let _ = tx.rollback().await;
    }

    Ok(())
}

/// Invokes your native HNSW Vector context indexing routine directly
async fn run_daily_rag_sync(rag_service: Arc<RagService>) {
    info!("[RAG-JOB] Triggering Google Drive ingestion synchronization workflow...");
    if let Err(e) = rag_service.sync_and_reindex_trusted_sources().await {
        error!("❌ Error executing automated Google Drive context synchronization: {:?}", e);
    } else {
        info!("Refreshed native vector layer indexes.");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Engine Initializers (Tokio Spawn Schedulers)
// ─────────────────────────────────────────────────────────────────────────────

/// Initializes your background runners inside dedicated async workers
pub fn start_scheduler(pool: PgPool, rag_service: Arc<RagService>) {
    // Worker 1: Recurring Transactions Execution (Interval: 5 minutes)
    let pool_tx = pool.clone();
    tokio::spawn(async move {
        info!("🔄 Transaction Processing Scheduler Engine Online. Configuration: Every 5 minutes.");
        let mut interval = tokio::time::interval(Duration::from_secs(5 * 60));
        interval.tick().await; 
        
        loop {
            interval.tick().await;
            if let Err(e) = generate_recurring_transactions(&pool_tx).await {
                error!("❌ Error in background recurring transactions transaction job: {:?}", e);
            }
        }
    });

    // Worker 2: Production Cron Runner (Fires daily at exactly 2:30 AM local server time)
    tokio::spawn(async move {
        info!("📅 RAG Context Automation Scheduler Engine Online. Target Profile: Daily at 02:30 AM.");
        loop {
            let now = Utc::now();
            let mut target = now
                .with_hour(2).unwrap()
                .with_minute(30).unwrap()
                .with_second(0).unwrap()
                .with_nanosecond(0).unwrap();

            if now >= target {
                target = target + chrono::Duration::days(1);
            }

            let duration_to_sleep = match (target - now).to_std() {
                Ok(d) => d,
                Err(_) => Duration::from_secs(60),
            };

            info!("[RAG-SCHEDULER] Next indexing sync scheduled in {} hours.", (duration_to_sleep.as_secs() as f64 / 3600.0).round());
            tokio::time::sleep(duration_to_sleep).await;

            run_daily_rag_sync(rag_service.clone()).await;
        }
    });
}