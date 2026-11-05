use std::sync::Arc;
use std::time::Duration;

use kinetib_chatbot::chatbot_service::rag_service::RagService;
use time::{Duration as TimeDuration, OffsetDateTime, Time};
use tracing::{error, info, warn};

async fn run_daily_rag_sync(rag_service: Arc<RagService>) {
    info!("[RAG-JOB] Triggering Google Drive ingestion synchronization...");
    if let Err(error) = rag_service.sync_and_reindex_trusted_sources().await {
        error!("[RAG-JOB] Automated synchronization failed: {:?}", error);
    } else {
        info!("[RAG-JOB] Trusted-source vector indexes refreshed.");
    }
}

/// Starts only the legacy RAG scheduler. Recurring transaction generation has
/// moved to the FinTally kernel and is deliberately not run by Axum anymore.
pub fn start_rag_scheduler(rag_service: Arc<RagService>) {
    tokio::spawn(async move {
        info!("RAG context automation scheduler online.");

        if !rag_service.has_indexes() {
            warn!("RAG indexes are missing; starting initial indexing.");
            run_daily_rag_sync(Arc::clone(&rag_service)).await;
        } else {
            info!("Existing RAG indexes found; skipping initial rebuild.");
        }

        loop {
            let now = OffsetDateTime::now_utc();
            let target_time = Time::from_hms(2, 30, 0).expect("02:30 is a valid UTC schedule time");
            let mut target = now.replace_time(target_time);
            if now >= target {
                target += TimeDuration::days(1);
            }

            let delay = target - now;
            let sleep = if delay.is_positive() {
                Duration::from_secs(delay.whole_seconds() as u64)
            } else {
                Duration::from_secs(60)
            };

            info!(
                next_sync_hours = sleep.as_secs_f64() / 3600.0,
                "RAG indexing sync scheduled"
            );
            tokio::time::sleep(sleep).await;
            run_daily_rag_sync(Arc::clone(&rag_service)).await;
        }
    });
}
