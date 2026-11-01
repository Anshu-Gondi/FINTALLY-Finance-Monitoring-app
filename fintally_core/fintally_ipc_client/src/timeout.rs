//! Runtime-specific timeout primitive used by native and browser clients.

use std::future::Future;
use std::time::Duration;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TimeoutElapsed;

#[cfg(not(target_arch = "wasm32"))]
pub async fn with_timeout<F>(duration: Duration, future: F) -> Result<F::Output, TimeoutElapsed>
where
    F: Future,
{
    tokio::time::timeout(duration, future)
        .await
        .map_err(|_| TimeoutElapsed)
}

#[cfg(target_arch = "wasm32")]
pub async fn with_timeout<F>(duration: Duration, future: F) -> Result<F::Output, TimeoutElapsed>
where
    F: Future,
{
    use futures_util::future::{select, Either};
    use futures_util::pin_mut;
    use gloo_timers::future::TimeoutFuture;

    let milliseconds = duration.as_millis().min(u128::from(u32::MAX)) as u32;
    let timer = TimeoutFuture::new(milliseconds);
    pin_mut!(future);
    pin_mut!(timer);
    match select(future, timer).await {
        Either::Left((output, _timer)) => Ok(output),
        Either::Right((_elapsed, _future)) => Err(TimeoutElapsed),
    }
}
