//! ASR worker loop, transcript event handoff, cancellation and bounded shutdown.

use super::types::*;
use crate::asr::backend::{AsrBackend, AsrError, BackendTranscript};
use crossbeam_channel::{select_biased, Receiver, SendTimeoutError, Sender};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tracing::{debug, error, warn};

const EVENT_SEND_RETRY: Duration = Duration::from_millis(10);

fn publish_event(
    event_tx: &Sender<AsrEvent>,
    stopping: &AtomicBool,
    counters: &RuntimeCounters,
    mut event: AsrEvent,
) -> Result<(), AsrError> {
    loop {
        if stopping.load(Ordering::Acquire) {
            return Err(AsrError::WorkerStopping);
        }
        match event_tx.send_timeout(event, EVENT_SEND_RETRY) {
            Ok(()) => {
                counters
                    .worker
                    .events_published
                    .fetch_add(1, Ordering::Relaxed);
                return Ok(());
            }
            Err(SendTimeoutError::Timeout(returned)) => event = returned,
            Err(SendTimeoutError::Disconnected(_)) => return Err(AsrError::ChannelDisconnected),
        }
    }
}

fn publish_marker(
    event_tx: &Sender<AsrEvent>,
    stopping: &AtomicBool,
    counters: &RuntimeCounters,
    session_id: u64,
    timestamp_ns: u64,
    sample_position: u64,
    kind: AsrEventKind,
) -> Result<(), AsrError> {
    publish_event(
        event_tx,
        stopping,
        counters,
        AsrEvent::marker(session_id, timestamp_ns, sample_position, kind),
    )
}

fn publish_backend_update(
    event_tx: &Sender<AsrEvent>,
    stopping: &AtomicBool,
    counters: &RuntimeCounters,
    session_id: u64,
    timestamp_ns: u64,
    sample_position: u64,
    update: BackendTranscript<'_>,
) -> Result<(), AsrError> {
    let event = AsrEvent::transcript(session_id, timestamp_ns, sample_position, update);
    if event.kind == AsrEventKind::TranscriptTooLong {
        warn!(
            session_id = session_id,
            "ASR transcript exceeded fixed event capacity; no truncation performed"
        );
    }
    publish_event(event_tx, stopping, counters, event)
}

fn session_meta(last: Option<(u64, u64, u64)>, session_id: u64) -> (u64, u64) {
    match last {
        Some((last_session, timestamp_ns, sample_position)) if last_session == session_id => {
            (timestamp_ns, sample_position)
        }
        _ => (0, 0),
    }
}

fn finish_backend_session(
    backend: &mut dyn AsrBackend,
    event_tx: &Sender<AsrEvent>,
    stopping: &AtomicBool,
    counters: &RuntimeCounters,
    session_id: u64,
    timestamp_ns: u64,
    sample_position: u64,
) -> Result<(), AsrError> {
    let mut sink = |update: BackendTranscript<'_>| {
        publish_backend_update(
            event_tx,
            stopping,
            counters,
            session_id,
            timestamp_ns,
            sample_position,
            update,
        )
    };
    backend.finish(&mut sink)
}

fn pending_cancel_index(
    pending: &[u64; MAX_INPUT_CAPACITY],
    count: usize,
    session_id: u64,
) -> Option<usize> {
    pending[..count].iter().position(|&id| id == session_id)
}

fn remove_pending_cancel(pending: &mut [u64; MAX_INPUT_CAPACITY], count: &mut usize, index: usize) {
    if index >= *count {
        return;
    }
    for slot in index..(*count - 1) {
        pending[slot] = pending[slot + 1];
    }
    *count -= 1;
    pending[*count] = 0;
}

fn prune_old_pending_cancels(
    pending: &mut [u64; MAX_INPUT_CAPACITY],
    count: &mut usize,
    before_session: u64,
) {
    let mut index = 0;
    while index < *count {
        if pending[index] < before_session {
            remove_pending_cancel(pending, count, index);
        } else {
            index += 1;
        }
    }
}

pub(super) fn run_worker(
    mut backend: Box<dyn AsrBackend>,
    sample_rate_hz: u32,
    audio_rx: Receiver<AsrAudioFrame>,
    control_rx: Receiver<ControlCommand>,
    event_tx: Sender<AsrEvent>,
    stopping: Arc<AtomicBool>,
    counters: Arc<RuntimeCounters>,
) {
    let mut active_session: Option<u64> = None;
    let mut latest_session_id = 0_u64;
    let mut ended_through = 0_u64;
    let mut last_frame_meta: Option<(u64, u64, u64)> = None;
    let mut pending_finish_session: Option<u64> = None;
    // A cancelled future session may already have PCM frames queued but not yet
    // become the active decoder session. Store only fixed-size IDs; capacity is
    // bounded by the maximum number of distinct sessions represented in the
    // input queue (at most one distinct session per queued frame).
    let mut pending_cancels = [0_u64; MAX_INPUT_CAPACITY];
    let mut pending_cancel_count = 0_usize;
    let mut fail_closed = false;
    let mut shutdown_requested = false;

    loop {
        if stopping.load(Ordering::Acquire) {
            break;
        }
        if !shutdown_requested && audio_rx.is_empty() {
            if let Some(session_id) = pending_finish_session {
                if active_session == Some(session_id) {
                    pending_finish_session = None;
                    let (timestamp_ns, sample_position) = session_meta(last_frame_meta, session_id);
                    if let Err(error) = finish_backend_session(
                        backend.as_mut(),
                        &event_tx,
                        &stopping,
                        &counters,
                        session_id,
                        timestamp_ns,
                        sample_position,
                    ) {
                        counters
                            .worker
                            .backend_failures
                            .fetch_add(1, Ordering::Relaxed);
                        error!(session_id = session_id, error = %error, "ASR backend finish failed");
                        let _ = publish_marker(
                            &event_tx,
                            &stopping,
                            &counters,
                            session_id,
                            timestamp_ns,
                            sample_position,
                            AsrEventKind::BackendFailure,
                        );
                        let _ = backend.cancel();
                    }
                    let _ = publish_marker(
                        &event_tx,
                        &stopping,
                        &counters,
                        session_id,
                        timestamp_ns,
                        sample_position,
                        AsrEventKind::SessionFinished,
                    );
                    active_session = None;
                    ended_through = ended_through.max(session_id);
                } else {
                    // The queue is empty and this session is not active. If it had
                    // accepted frames, FIFO processing would have made it active
                    // before reaching this point; clear a stale/never-started finish.
                    pending_finish_session = None;
                }
            }
        }
        if shutdown_requested && audio_rx.is_empty() {
            if let Some(session_id) = active_session.take() {
                let (timestamp_ns, sample_position) = session_meta(last_frame_meta, session_id);
                if let Err(error) = finish_backend_session(
                    backend.as_mut(),
                    &event_tx,
                    &stopping,
                    &counters,
                    session_id,
                    timestamp_ns,
                    sample_position,
                ) {
                    counters
                        .worker
                        .backend_failures
                        .fetch_add(1, Ordering::Relaxed);
                    error!(session_id = session_id, error = %error, "ASR backend final flush failed during shutdown");
                    let _ = publish_marker(
                        &event_tx,
                        &stopping,
                        &counters,
                        session_id,
                        timestamp_ns,
                        sample_position,
                        AsrEventKind::BackendFailure,
                    );
                }
            }
            let (timestamp_ns, sample_position) = session_meta(last_frame_meta, latest_session_id);
            let _ = publish_marker(
                &event_tx,
                &stopping,
                &counters,
                latest_session_id,
                timestamp_ns,
                sample_position,
                AsrEventKind::WorkerStopped,
            );
            break;
        }

        // Control is biased over audio, so cancellation is serviced before more
        // queued audio whenever both channels are ready.
        select_biased! {
            recv(control_rx) -> command => {
                let Ok(command) = command else { break };
                match command {
                    ControlCommand::Cancel { session_id } => {
                        if active_session == Some(session_id) {
                            let (timestamp_ns, sample_position) = session_meta(last_frame_meta, session_id);
                            if let Err(error) = backend.cancel() {
                                counters.worker.backend_failures.fetch_add(1, Ordering::Relaxed);
                                error!(session_id = session_id, error = %error, "ASR backend cancel failed");
                                let _ = publish_marker(&event_tx, &stopping, &counters, session_id, timestamp_ns, sample_position, AsrEventKind::BackendFailure);
                            } else {
                                let _ = publish_marker(&event_tx, &stopping, &counters, session_id, timestamp_ns, sample_position, AsrEventKind::SessionCancelled);
                            }
                            active_session = None;
                            ended_through = ended_through.max(session_id);
                            if pending_finish_session == Some(session_id) {
                                pending_finish_session = None;
                            }
                        } else if session_id > ended_through && session_id > latest_session_id {
                            // The session has not started decoding yet. Remember its
                            // cancellation so queued frames cannot resurrect it.
                            if pending_cancel_index(&pending_cancels, pending_cancel_count, session_id).is_none() {
                                if pending_cancel_count < pending_cancels.len() {
                                    pending_cancels[pending_cancel_count] = session_id;
                                    pending_cancel_count += 1;
                                } else {
                                    // Under the API contract this cannot happen: there
                                    // cannot be more queued distinct sessions than frames.
                                    // If a caller submits arbitrary future IDs anyway,
                                    // stop decoding rather than risk resurrecting cancelled
                                    // financial speech with incomplete cancellation state.
                                    warn!(session_id = session_id, "pending ASR cancel set saturated; disabling decoding until runtime restart");
                                    fail_closed = true;
                                    if let Some(active_id) = active_session.take() {
                                        let (ts, pos) = session_meta(last_frame_meta, active_id);
                                        let _ = backend.cancel();
                                        ended_through = ended_through.max(active_id);
                                        let _ = publish_marker(&event_tx, &stopping, &counters, active_id, ts, pos, AsrEventKind::BackendFailure);
                                    }
                                    let _ = publish_marker(&event_tx, &stopping, &counters, session_id, 0, 0, AsrEventKind::BackendFailure);
                                }
                            }
                            if pending_finish_session == Some(session_id) {
                                pending_finish_session = None;
                            }
                        }
                    }
                    ControlCommand::Finish { session_id } => {
                        // A finish request is deferred until the audio queue drains.
                        // This prevents a high-priority control message from flushing
                        // before earlier accepted audio frames have been decoded.
                        if session_id > ended_through
                            && (active_session == Some(session_id)
                                || (session_id >= latest_session_id && !audio_rx.is_empty()))
                        {
                            pending_finish_session = Some(session_id);
                        }
                    }
                    ControlCommand::Shutdown => {
                        // Drain frames already accepted before finalizing. New calls
                        // are rejected by the runtime as soon as shutdown is requested.
                        shutdown_requested = true;
                    }
                }
            }
            recv(audio_rx) -> frame_result => {
                let Ok(frame) = frame_result else { break };
                if fail_closed {
                    counters.worker.stale_frames_ignored.fetch_add(1, Ordering::Relaxed);
                    debug!(session_id = frame.session_id, "dropping ASR frame while cancellation state is fail-closed");
                    continue;
                }
                if frame.session_id <= ended_through || frame.session_id < latest_session_id {
                    counters.worker.stale_frames_ignored.fetch_add(1, Ordering::Relaxed);
                    debug!(session_id = frame.session_id, "ignoring ASR frame for an ended or stale session");
                    continue;
                }
                prune_old_pending_cancels(
                    &mut pending_cancels,
                    &mut pending_cancel_count,
                    frame.session_id,
                );
                let cancelled_before_start = pending_cancel_index(
                    &pending_cancels,
                    pending_cancel_count,
                    frame.session_id,
                );

                if active_session != Some(frame.session_id) {
                    if let Some(previous_session) = active_session.take() {
                        let (previous_timestamp_ns, previous_sample_position) = session_meta(last_frame_meta, previous_session);
                        if pending_finish_session == Some(previous_session) {
                            // All frames for this session before the new session are
                            // already processed (the input queue preserves FIFO order).
                            pending_finish_session = None;
                            if let Err(error) = finish_backend_session(
                                backend.as_mut(), &event_tx, &stopping, &counters,
                                previous_session, previous_timestamp_ns, previous_sample_position,
                            ) {
                                counters.worker.backend_failures.fetch_add(1, Ordering::Relaxed);
                                error!(session_id = previous_session, error = %error, "ASR backend finish failed at session boundary");
                                let _ = publish_marker(&event_tx, &stopping, &counters, previous_session, previous_timestamp_ns, previous_sample_position, AsrEventKind::BackendFailure);
                                let _ = backend.cancel();
                            }
                            let _ = publish_marker(&event_tx, &stopping, &counters, previous_session, previous_timestamp_ns, previous_sample_position, AsrEventKind::SessionFinished);
                        } else {
                            if let Err(error) = backend.cancel() {
                                counters.worker.backend_failures.fetch_add(1, Ordering::Relaxed);
                                error!(session_id = previous_session, error = %error, "ASR backend reset failed on session change");
                                let _ = publish_marker(&event_tx, &stopping, &counters, previous_session, previous_timestamp_ns, previous_sample_position, AsrEventKind::BackendFailure);
                            }
                            let _ = publish_marker(&event_tx, &stopping, &counters, previous_session, previous_timestamp_ns, previous_sample_position, AsrEventKind::SessionCancelled);
                        }
                        ended_through = ended_through.max(previous_session);
                    }
                    latest_session_id = frame.session_id;
                    if let Some(index) = cancelled_before_start {
                        remove_pending_cancel(&mut pending_cancels, &mut pending_cancel_count, index);
                        ended_through = ended_through.max(frame.session_id);
                        if pending_finish_session == Some(frame.session_id) {
                            pending_finish_session = None;
                        }
                        counters.worker.stale_frames_ignored.fetch_add(1, Ordering::Relaxed);
                        let _ = publish_marker(
                            &event_tx,
                            &stopping,
                            &counters,
                            frame.session_id,
                            frame.timestamp_ns,
                            frame.sample_position,
                            AsrEventKind::SessionCancelled,
                        );
                        continue;
                    }
                    active_session = Some(frame.session_id);
                }
                last_frame_meta = Some((frame.session_id, frame.timestamp_ns, frame.sample_position));

                let mut sink = |update: BackendTranscript<'_>| publish_backend_update(
                    &event_tx,
                    &stopping,
                    &counters,
                    frame.session_id,
                    frame.timestamp_ns,
                    frame.sample_position,
                    update,
                );
                let service_started = Instant::now();
                let decode_result = backend.push_audio(&frame.samples, sample_rate_hz, &mut sink);
                let elapsed_ns = u64::try_from(service_started.elapsed().as_nanos()).unwrap_or(u64::MAX);
                counters.worker.frame_service_time_ns_total.fetch_add(elapsed_ns, Ordering::Relaxed);
                counters.worker.frame_service_time_ns_max.fetch_max(elapsed_ns, Ordering::Relaxed);
                match decode_result {
                    Ok(()) => {
                        counters.worker.frames_processed.fetch_add(1, Ordering::Relaxed);
                    }
                    Err(error) => {
                        counters.worker.backend_failures.fetch_add(1, Ordering::Relaxed);
                        error!(session_id = frame.session_id, error = %error, "ASR backend failed while decoding frame");
                        let _ = publish_marker(&event_tx, &stopping, &counters, frame.session_id, frame.timestamp_ns, frame.sample_position, AsrEventKind::BackendFailure);
                        let _ = backend.cancel();
                        active_session = None;
                        ended_through = ended_through.max(frame.session_id);
                    }
                }
            }
            default(Duration::from_millis(20)) => {}
        }
    }
}
