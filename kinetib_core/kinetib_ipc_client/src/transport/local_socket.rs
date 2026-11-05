//! Native local transports.
//!
//! Unix-domain sockets cover Linux/macOS and may be used by an app-hosted
//! service on Android/iOS. Windows uses a named-pipe client. Whether a separate
//! kernel process is appropriate on mobile is an app packaging decision; see
//! `PLATFORMS.md`.

#[cfg(any(unix, windows))]
use crate::client::IpcClient;
#[cfg(any(unix, windows))]
use crate::config::ClientConfig;
#[cfg(any(unix, windows))]
use crate::error::ClientError;
#[cfg(not(target_arch = "wasm32"))]
use crate::timeout::with_timeout;
#[cfg(any(unix, windows))]
use crate::transport::FramedConnection;

#[cfg(unix)]
pub async fn connect_unix(
    path: impl AsRef<std::path::Path>,
    config: ClientConfig,
) -> Result<IpcClient<FramedConnection<tokio::net::UnixStream>>, ClientError> {
    let stream = with_timeout(
        config.connect_timeout(),
        tokio::net::UnixStream::connect(path),
    )
    .await
    .map_err(|_| ClientError::ConnectTimeout)?
    .map_err(|_| ClientError::ConnectFailed)?;
    Ok(IpcClient::new(FramedConnection::new(stream), config))
}

#[cfg(windows)]
pub async fn connect_named_pipe(
    pipe_name: &str,
    config: ClientConfig,
) -> Result<
    IpcClient<FramedConnection<tokio::net::windows::named_pipe::NamedPipeClient>>,
    ClientError,
> {
    use std::io;
    use std::time::Duration;
    use tokio::net::windows::named_pipe::ClientOptions;

    let name = pipe_name.to_owned();
    let connect = async move {
        loop {
            match ClientOptions::new().open(&name) {
                Ok(pipe) => return Ok(pipe),
                Err(error)
                    if error.kind() == io::ErrorKind::NotFound
                        || error.raw_os_error() == Some(231) =>
                {
                    // ERROR_PIPE_BUSY is 231. Retry briefly until the bounded
                    // outer connect timeout expires.
                    tokio::time::sleep(Duration::from_millis(10)).await;
                }
                Err(_) => return Err(ClientError::ConnectFailed),
            }
        }
    };

    let pipe = with_timeout(config.connect_timeout(), connect)
        .await
        .map_err(|_| ClientError::ConnectTimeout)??;
    Ok(IpcClient::new(FramedConnection::new(pipe), config))
}
