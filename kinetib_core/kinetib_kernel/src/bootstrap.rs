//! Kernel startup wiring: config, database pool, sessions, socket and shutdown.

#[path = "jobs/mod.rs"]
mod jobs;

use crate::config::KernelConfig;
use crate::ipc::listener;
use crate::observability::{metrics::Metrics, tracing::init_tracing};
use crate::shutdown::install_shutdown_signal;
use crate::state::KernelState;
use kinetib_db::DbContext;
use kinetib_db_local::LocalDbContext;
use sqlx::postgres::{PgConnectOptions, PgPoolOptions};
use std::error::Error;
use std::str::FromStr;
use std::sync::Arc;
use std::time::Duration;

pub async fn run() -> Result<(), Box<dyn Error + Send + Sync>> {
    init_tracing()?;
    let config = KernelConfig::from_env()?;
    tracing::info!(socket = %config.socket_path.display(), "starting FinTally kernel");

    let db = if let Some(database_url) = config.database_url.as_deref() {
        // Lazy pool creation is deliberate: a LocalOnly signup/login never
        // opens a network connection to Supabase. The first CloudSync query
        // is the first point at which the pool connects.
        let connection_options = PgConnectOptions::from_str(database_url)
            .map_err(|_| std::io::Error::other("invalid cloud database configuration"))?
            .statement_cache_capacity(0);
        let pool = PgPoolOptions::new()
            .max_connections(config.db_max_connections)
            .connect_lazy_with(connection_options);
        Some(DbContext { pool })
    } else {
        tracing::info!("DATABASE_URL is not configured; cloud storage is disabled");
        None
    };
    let local_db = LocalDbContext::connect(&config.local_db_path).await?;

    let metrics = Arc::new(Metrics::default());
    let state = KernelState::new(db, local_db, &config, Arc::clone(&metrics))?;
    let (listener, _socket_guard) = listener::bind(&config.socket_path)?;
    let shutdown = install_shutdown_signal();

    // The recurring worker is owned by the kernel lifecycle, not the UI or
    // Axum process. LocalOnly is always serviced; CloudSync is optional.
    let (job_shutdown_tx, job_shutdown_rx) = tokio::sync::watch::channel(false);
    let recurrence_worker = jobs::recurring_transactions::start_recurring_scheduler(
        state.db.clone(),
        state.local_db.clone(),
        Arc::clone(&state.cloud_recurring_schema_ready),
        job_shutdown_rx,
    );

    tracing::info!("FinTally kernel IPC listener ready");
    let listener_result = listener::run(
        listener,
        state,
        shutdown,
        config.max_connections,
        Arc::clone(&metrics),
    )
    .await;

    // Stop the scheduler whether the listener ended normally or with an error.
    let _ = job_shutdown_tx.send(true);
    match tokio::time::timeout(Duration::from_secs(5), recurrence_worker).await {
        Ok(Ok(())) => {}
        Ok(Err(error)) => {
            tracing::warn!(error = %error, "Recurring worker task failed during shutdown")
        }
        Err(_) => tracing::warn!("Recurring worker did not stop within the shutdown grace period"),
    }
    listener_result?;

    let snapshot = metrics.snapshot();
    tracing::info!(
        accepted_connections = snapshot.accepted_connections,
        requests_received = snapshot.requests_received,
        responses_sent = snapshot.responses_sent,
        rejected_frames = snapshot.rejected_frames,
        authentication_failures = snapshot.authentication_failures,
        dispatch_errors = snapshot.dispatch_errors,
        "FinTally kernel stopped"
    );
    Ok(())
}
