//! Unix-domain listener with bounded concurrent connections and restrictive permissions.

use crate::ipc::connection::serve_connection;
use crate::observability::metrics::Metrics;
use crate::state::KernelState;
use std::io;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;
use thiserror::Error;
use tokio::net::UnixListener;
use tokio::sync::{watch, Semaphore};
use tokio::task::JoinSet;
use tokio::time::timeout;

#[derive(Debug, Error)]
pub enum ListenerError {
    #[error("unable to bind IPC socket")]
    Bind(#[from] io::Error),
}

/// Owns cleanup of a socket path created by this process. It intentionally
/// does not unlink an existing path, which might belong to a live kernel.
pub struct SocketPathGuard {
    path: PathBuf,
}

impl Drop for SocketPathGuard {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.path);
    }
}

pub fn bind(path: &Path) -> Result<(UnixListener, SocketPathGuard), ListenerError> {
    if path.exists() {
        return Err(ListenerError::Bind(io::Error::new(
            io::ErrorKind::AddrInUse,
            "IPC socket path already exists; verify no kernel is running before removing a stale path",
        )));
    }
    let listener = UnixListener::bind(path)?;
    if let Err(error) = std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o600)) {
        drop(listener);
        let _ = std::fs::remove_file(path);
        return Err(ListenerError::Bind(error));
    }
    Ok((
        listener,
        SocketPathGuard {
            path: path.to_owned(),
        },
    ))
}

pub async fn run(
    listener: UnixListener,
    state: Arc<KernelState>,
    mut shutdown: watch::Receiver<bool>,
    max_connections: usize,
    metrics: Arc<Metrics>,
) -> Result<(), ListenerError> {
    let permits = Arc::new(Semaphore::new(max_connections.max(1)));
    let mut connections = JoinSet::new();
    loop {
        tokio::select! {
            changed = shutdown.changed() => {
                if changed.is_err() || *shutdown.borrow() {
                    break;
                }
            }
            joined = connections.join_next(), if !connections.is_empty() => {
                if let Some(Err(error)) = joined {
                    tracing::debug!(error = %error, "IPC connection task failed");
                }
            }
            accepted = listener.accept() => {
                let (stream, _peer) = accepted?;
                let permit = match Arc::clone(&permits).try_acquire_owned() {
                    Ok(permit) => permit,
                    Err(_) => {
                        tracing::warn!("connection limit reached; dropping new IPC connection");
                        continue;
                    }
                };
                metrics.connection_accepted();
                let state = Arc::clone(&state);
                let metrics = Arc::clone(&metrics);
                connections.spawn(async move {
                    let _permit = permit;
                    if let Err(error) = serve_connection(stream, state, metrics).await {
                        tracing::debug!(error = %error, "IPC connection closed");
                    }
                });
            }
        }
    }

    // Allow active requests to complete on shutdown, but do not let stuck local
    // clients hold process shutdown indefinitely.
    let drain = async { while connections.join_next().await.is_some() {} };
    if timeout(Duration::from_secs(5), drain).await.is_err() {
        connections.abort_all();
        while connections.join_next().await.is_some() {}
    }
    Ok(())
}
