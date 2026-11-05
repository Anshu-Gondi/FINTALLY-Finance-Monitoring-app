//! Graceful Ctrl-C/SIGTERM shutdown signal shared with the listener.

use tokio::sync::watch;

pub fn install_shutdown_signal() -> watch::Receiver<bool> {
    let (sender, receiver) = watch::channel(false);
    let _signal_task = tokio::spawn(async move {
        #[cfg(unix)]
        {
            use tokio::signal::unix::{signal, SignalKind};
            match signal(SignalKind::terminate()) {
                Ok(mut terminate) => {
                    tokio::select! {
                        result = tokio::signal::ctrl_c() => {
                            if let Err(error) = result {
                                tracing::error!(error = %error, "failed to listen for Ctrl-C; requesting shutdown");
                            } else {
                                tracing::info!("Ctrl-C received; requesting shutdown");
                            }
                        }
                        _ = terminate.recv() => {
                            tracing::info!("SIGTERM received; requesting shutdown");
                        }
                    }
                }
                Err(error) => {
                    tracing::error!(error = %error, "failed to register SIGTERM handler");
                    if let Err(ctrl_c_error) = tokio::signal::ctrl_c().await {
                        tracing::error!(error = %ctrl_c_error, "failed to listen for Ctrl-C; requesting shutdown");
                    }
                }
            }
        }

        #[cfg(not(unix))]
        if let Err(error) = tokio::signal::ctrl_c().await {
            tracing::error!(error = %error, "failed to listen for Ctrl-C; requesting shutdown");
        }

        let _ = sender.send(true);
    });
    receiver
}
