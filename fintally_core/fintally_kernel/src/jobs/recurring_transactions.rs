//! Kernel-owned recurring-transaction scheduler for LocalOnly and CloudSync.
//!
//! The two stores run in independent tasks. A slow or unavailable CloudSync
//! database cannot delay the device-local SQLite schedule worker.

use fintally_app::transactions::recurrence::{self as recurrence, BatchStats};
use fintally_db::DbContext;
use fintally_db_local::LocalDbContext;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use tokio::sync::watch;
use tokio::task::JoinHandle;
use tokio::time::MissedTickBehavior;

const SCHEDULER_INTERVAL: Duration = Duration::from_secs(5 * 60);
const MAX_TEMPLATES_PER_BATCH: i64 = 16;
const MAX_CATCH_UP_PER_TEMPLATE: u8 = 3;
const MAX_BATCHES_PER_TICK: usize = 4;

pub(crate) fn start_recurring_scheduler(
    cloud_db: Option<DbContext>,
    local_db: LocalDbContext,
    cloud_schema_ready: Arc<AtomicBool>,
    shutdown: watch::Receiver<bool>,
) -> JoinHandle<()> {
    tokio::spawn(async move {
        let local_shutdown = shutdown.clone();
        let local_worker = tokio::spawn(run_local_worker(local_db, local_shutdown));

        let Some(cloud_db) = cloud_db else {
            let _ = local_worker.await;
            return;
        };

        let cloud_worker = tokio::spawn(run_cloud_worker(cloud_db, cloud_schema_ready, shutdown));
        let (local_result, cloud_result) = tokio::join!(local_worker, cloud_worker);
        if let Err(error) = local_result {
            tracing::warn!(error = %error, "LocalOnly recurrence worker task stopped unexpectedly");
        }
        if let Err(error) = cloud_result {
            tracing::warn!(error = %error, "CloudSync recurrence worker task stopped unexpectedly");
        }
    })
}

async fn run_local_worker(db: LocalDbContext, mut shutdown: watch::Receiver<bool>) {
    tracing::info!(
        cadence_seconds = SCHEDULER_INTERVAL.as_secs(),
        "LocalOnly recurring transaction worker online"
    );
    let mut interval = tokio::time::interval(SCHEDULER_INTERVAL);
    interval.set_missed_tick_behavior(MissedTickBehavior::Delay);
    let mut cursor = 0_i64;

    loop {
        tokio::select! {
            changed = shutdown.changed() => {
                if changed.is_err() || *shutdown.borrow() {
                    tracing::info!("LocalOnly recurring transaction worker stopping");
                    break;
                }
            }
            _ = interval.tick() => {
                let Some(now) = unix_now() else {
                    tracing::warn!("System clock is before Unix epoch; LocalOnly recurrence tick skipped");
                    continue;
                };
                let batch_result = tokio::select! {
                    result = run_local_batches(&db, now, &mut cursor) => Some(result),
                    changed = shutdown.changed() => {
                        if changed.is_err() || *shutdown.borrow() {
                            tracing::info!("LocalOnly recurring worker stopping during a batch");
                            break;
                        }
                        None
                    }
                };
                if let Some(Err(error)) = batch_result {
                    tracing::warn!(error = %error, "LocalOnly recurring batch failed; it will be retried");
                }
            }
        }
    }
}

async fn run_cloud_worker(
    db: DbContext,
    schema_ready: Arc<AtomicBool>,
    mut shutdown: watch::Receiver<bool>,
) {
    tracing::info!(
        cadence_seconds = SCHEDULER_INTERVAL.as_secs(),
        "CloudSync recurring transaction worker online"
    );
    let mut interval = tokio::time::interval(SCHEDULER_INTERVAL);
    interval.set_missed_tick_behavior(MissedTickBehavior::Delay);
    let mut cursor = 0_i64;
    let mut migration_complete = false;

    loop {
        tokio::select! {
            changed = shutdown.changed() => {
                if changed.is_err() || *shutdown.borrow() {
                    tracing::info!("CloudSync recurring transaction worker stopping");
                    break;
                }
            }
            _ = interval.tick() => {
                let Some(now) = unix_now() else {
                    tracing::warn!("System clock is before Unix epoch; CloudSync recurrence tick skipped");
                    continue;
                };

                if !migration_complete {
                    let migration_result = tokio::select! {
                        result = fintally_db::migrate_cloud_schema(&db.pool) => Some(result),
                        changed = shutdown.changed() => {
                            if changed.is_err() || *shutdown.borrow() {
                                tracing::info!("CloudSync recurring worker stopping during migration");
                                break;
                            }
                            None
                        }
                    };
                    let Some(migration_result) = migration_result else {
                        continue;
                    };
                    match migration_result {
                        Ok(()) => {
                            migration_complete = true;
                            schema_ready.store(true, Ordering::Release);
                            tracing::info!("CloudSync recurring schema migration is ready");
                        }
                        Err(error) => {
                            tracing::warn!(error = %error, "CloudSync recurrence migration failed; retrying on a later tick");
                            continue;
                        }
                    }
                }

                let batch_result = tokio::select! {
                    result = run_cloud_batches(&db, now, &mut cursor) => Some(result),
                    changed = shutdown.changed() => {
                        if changed.is_err() || *shutdown.borrow() {
                            tracing::info!("CloudSync recurring worker stopping during a batch");
                            break;
                        }
                        None
                    }
                };
                if let Some(Err(error)) = batch_result {
                    tracing::warn!(error = %error, "CloudSync recurring batch failed; it will be retried");
                }
            }
        }
    }
}

async fn run_local_batches(
    db: &LocalDbContext,
    now_unix_seconds: i64,
    cursor: &mut i64,
) -> Result<(), fintally_db_local::transactions::recurrence::RecurrenceRepositoryError> {
    for _ in 0..MAX_BATCHES_PER_TICK {
        let stats = recurrence::process_local_batch(
            db,
            now_unix_seconds,
            MAX_TEMPLATES_PER_BATCH,
            MAX_CATCH_UP_PER_TEMPLATE,
            *cursor,
        )
        .await?;
        advance_cursor(cursor, stats);
        log_batch("LocalOnly", stats);
        if i64::from(stats.templates_scanned) < MAX_TEMPLATES_PER_BATCH {
            break;
        }
        tokio::task::yield_now().await;
    }
    Ok(())
}

async fn run_cloud_batches(
    db: &DbContext,
    now_unix_seconds: i64,
    cursor: &mut i64,
) -> Result<(), fintally_db::transactions::recurrence::RecurrenceRepositoryError> {
    for _ in 0..MAX_BATCHES_PER_TICK {
        let stats = recurrence::process_cloud_batch(
            db,
            now_unix_seconds,
            MAX_TEMPLATES_PER_BATCH,
            MAX_CATCH_UP_PER_TEMPLATE,
            *cursor,
        )
        .await?;
        advance_cursor(cursor, stats);
        log_batch("CloudSync", stats);
        if i64::from(stats.templates_scanned) < MAX_TEMPLATES_PER_BATCH {
            break;
        }
        tokio::task::yield_now().await;
    }
    Ok(())
}

fn advance_cursor(cursor: &mut i64, stats: BatchStats) {
    if let Some(last_template_id) = stats.last_template_id {
        *cursor = last_template_id;
    }
}

fn log_batch(storage_scope: &'static str, stats: BatchStats) {
    if stats.occurrences_created > 0 || stats.schedules_completed > 0 {
        tracing::info!(
            storage_scope,
            templates_scanned = stats.templates_scanned,
            occurrences_created = stats.occurrences_created,
            schedules_completed = stats.schedules_completed,
            "Recurring transaction batch completed"
        );
    }
}

fn unix_now() -> Option<i64> {
    let seconds = SystemTime::now().duration_since(UNIX_EPOCH).ok()?.as_secs();
    i64::try_from(seconds).ok()
}

#[cfg(test)]
mod tests {
    use super::unix_now;

    #[test]
    fn unix_clock_is_non_negative_and_fits_persisted_timestamp() {
        assert!(unix_now().is_some_and(|now| now > 0));
    }
}
