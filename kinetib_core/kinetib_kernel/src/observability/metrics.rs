//! Low-overhead process metrics without unbounded labels or sensitive values.

use std::sync::atomic::{AtomicU64, Ordering};

#[derive(Default)]
pub struct Metrics {
    accepted_connections: AtomicU64,
    requests_received: AtomicU64,
    responses_sent: AtomicU64,
    rejected_frames: AtomicU64,
    authentication_failures: AtomicU64,
    dispatch_errors: AtomicU64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MetricsSnapshot {
    pub accepted_connections: u64,
    pub requests_received: u64,
    pub responses_sent: u64,
    pub rejected_frames: u64,
    pub authentication_failures: u64,
    pub dispatch_errors: u64,
}

impl Metrics {
    pub fn connection_accepted(&self) {
        self.accepted_connections.fetch_add(1, Ordering::Relaxed);
    }
    pub fn request_received(&self) {
        self.requests_received.fetch_add(1, Ordering::Relaxed);
    }
    pub fn response_sent(&self) {
        self.responses_sent.fetch_add(1, Ordering::Relaxed);
    }
    pub fn frame_rejected(&self) {
        self.rejected_frames.fetch_add(1, Ordering::Relaxed);
    }
    pub fn authentication_failed(&self) {
        self.authentication_failures.fetch_add(1, Ordering::Relaxed);
    }
    pub fn dispatch_error(&self) {
        self.dispatch_errors.fetch_add(1, Ordering::Relaxed);
    }

    pub fn snapshot(&self) -> MetricsSnapshot {
        MetricsSnapshot {
            accepted_connections: self.accepted_connections.load(Ordering::Relaxed),
            requests_received: self.requests_received.load(Ordering::Relaxed),
            responses_sent: self.responses_sent.load(Ordering::Relaxed),
            rejected_frames: self.rejected_frames.load(Ordering::Relaxed),
            authentication_failures: self.authentication_failures.load(Ordering::Relaxed),
            dispatch_errors: self.dispatch_errors.load(Ordering::Relaxed),
        }
    }
}
