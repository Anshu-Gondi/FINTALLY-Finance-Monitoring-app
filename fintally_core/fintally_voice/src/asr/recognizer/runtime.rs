//! Public ASR runtime handle and non-blocking submit/poll API.

use super::types::*;
use super::worker::run_worker;
use crate::asr::backend::{create_backend, AsrBackend, AsrBackendConfig, AsrError};
use crossbeam_channel::{bounded, Receiver, Sender, TryRecvError, TrySendError};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread::{self, JoinHandle};

pub struct AsrRuntime {
    config: AsrRuntimeConfig,
    audio_tx: Sender<AsrAudioFrame>,
    control_tx: Sender<ControlCommand>,
    event_rx: Receiver<AsrEvent>,
    stopping: Arc<AtomicBool>,
    accepting_audio: AtomicBool,
    counters: Arc<RuntimeCounters>,
    worker: Option<JoinHandle<()>>,
}

impl AsrRuntime {
    /// Spawn the runtime and construct the configured backend inside the ASR
    /// worker. With the default Cargo features this returns
    /// `BackendFeatureDisabled`; enable `asr-sherpa-onnx` for Sherpa-ONNX.
    pub fn spawn(
        config: AsrRuntimeConfig,
        backend_config: AsrBackendConfig,
    ) -> Result<Self, AsrError> {
        Self::spawn_with_factory(config, move || create_backend(backend_config))
    }

    /// Factory entry point for tests and alternate native backends. The factory
    /// runs on the ASR worker thread. A backend instance is never sent between
    /// threads and need not be Send/Sync.
    pub fn spawn_with_factory<F>(config: AsrRuntimeConfig, factory: F) -> Result<Self, AsrError>
    where
        F: FnOnce() -> Result<Box<dyn AsrBackend>, AsrError> + Send + 'static,
    {
        config.validate()?;
        let (audio_tx, audio_rx) = bounded(config.input_capacity);
        let (control_tx, control_rx) = bounded(config.control_capacity);
        let (event_tx, event_rx) = bounded(config.output_capacity);
        let (ready_tx, ready_rx) = bounded::<Result<(), AsrError>>(1);
        let stopping = Arc::new(AtomicBool::new(false));
        let worker_stopping = Arc::clone(&stopping);
        let counters = Arc::new(RuntimeCounters::default());
        let worker_counters = Arc::clone(&counters);
        let sample_rate_hz = config.sample_rate_hz;

        let worker = thread::Builder::new()
            .name("fintally-asr".to_owned())
            .spawn(move || {
                let backend = match factory() {
                    Ok(backend) => {
                        let _ = ready_tx.send(Ok(()));
                        backend
                    }
                    Err(error) => {
                        let _ = ready_tx.send(Err(error));
                        return;
                    }
                };
                run_worker(
                    backend,
                    sample_rate_hz,
                    audio_rx,
                    control_rx,
                    event_tx,
                    worker_stopping,
                    worker_counters,
                );
            })
            .map_err(|error| AsrError::WorkerThread(error.to_string()))?;

        match ready_rx.recv() {
            Ok(Ok(())) => Ok(Self {
                config,
                audio_tx,
                control_tx,
                event_rx,
                stopping,
                accepting_audio: AtomicBool::new(true),
                counters,
                worker: Some(worker),
            }),
            Ok(Err(error)) => {
                let _ = worker.join();
                Err(error)
            }
            Err(_) => {
                let _ = worker.join();
                Err(AsrError::ChannelDisconnected)
            }
        }
    }

    pub fn config(&self) -> &AsrRuntimeConfig {
        &self.config
    }

    pub fn memory_report(&self) -> AsrMemoryReport {
        self.config.memory_report()
    }

    pub fn stats(&self) -> AsrStatsSnapshot {
        self.counters.snapshot()
    }

    /// Non-blocking submission. On saturation, the input frame is returned to
    /// the caller for explicit overrun handling; this method never waits for ASR.
    pub fn try_submit_frame(&self, frame: AsrAudioFrame) -> Result<(), AudioSubmitError> {
        if self.stopping.load(Ordering::Acquire) || !self.accepting_audio.load(Ordering::Acquire) {
            return Err(AudioSubmitError::WorkerStopped(frame));
        }
        match self.audio_tx.try_send(frame) {
            Ok(()) => {
                self.counters
                    .producer
                    .frames_accepted
                    .fetch_add(1, Ordering::Relaxed);
                Ok(())
            }
            Err(TrySendError::Full(frame)) => {
                self.counters
                    .producer
                    .input_queue_full
                    .fetch_add(1, Ordering::Relaxed);
                Err(AudioSubmitError::QueueFull(frame))
            }
            Err(TrySendError::Disconnected(frame)) => Err(AudioSubmitError::WorkerStopped(frame)),
        }
    }

    /// Convenience API for callers holding their capture frame's PCM16 slice.
    pub fn try_submit_pcm16(
        &self,
        session_id: u64,
        timestamp_ns: u64,
        sample_position: u64,
        pcm16: &[i16],
    ) -> Result<(), AsrSubmitFrameError> {
        let frame = AsrAudioFrame::new(session_id, timestamp_ns, sample_position, pcm16)
            .map_err(AsrSubmitFrameError::InvalidFrame)?;
        self.try_submit_frame(frame)
            .map_err(AsrSubmitFrameError::Queue)
    }

    pub fn cancel_session(&self, session_id: u64) -> Result<(), AsrControlError> {
        self.send_control(ControlCommand::Cancel { session_id })
    }

    pub fn finish_session(&self, session_id: u64) -> Result<(), AsrControlError> {
        self.send_control(ControlCommand::Finish { session_id })
    }

    fn send_control(&self, command: ControlCommand) -> Result<(), AsrControlError> {
        if self.stopping.load(Ordering::Acquire) || !self.accepting_audio.load(Ordering::Acquire) {
            return Err(AsrControlError::WorkerStopped);
        }
        match self.control_tx.try_send(command) {
            Ok(()) => Ok(()),
            Err(TrySendError::Full(_)) => Err(AsrControlError::QueueFull),
            Err(TrySendError::Disconnected(_)) => Err(AsrControlError::WorkerStopped),
        }
    }

    /// Poll output without blocking. The UI/service layer should poll on its own
    /// thread and keep draining this bounded queue.
    pub fn try_recv_event(&self) -> Option<AsrEvent> {
        match self.event_rx.try_recv() {
            Ok(event) => Some(event),
            Err(TryRecvError::Empty | TryRecvError::Disconnected) => None,
        }
    }

    /// Request graceful stop; final transcript events are emitted before the
    /// worker-stopped event. Drain events while waiting for `join`.
    pub fn request_shutdown(&self) -> Result<(), AsrControlError> {
        if self.stopping.load(Ordering::Acquire) {
            return Err(AsrControlError::WorkerStopped);
        }
        // Stop new submissions before enqueueing shutdown. The caller should stop
        // its capture producer first; the worker then drains all already-accepted
        // frames before flushing the final transcript.
        if !self.accepting_audio.swap(false, Ordering::AcqRel) {
            return Ok(()); // idempotent after the first shutdown request
        }
        self.control_tx
            .send(ControlCommand::Shutdown)
            .map_err(|_| AsrControlError::WorkerStopped)
    }

    /// Join only after the consumer has drained enough events for the worker to
    /// make progress. Output backpressure intentionally never discards transcript
    /// events during a graceful shutdown.
    pub fn join(&mut self) -> Result<(), AsrError> {
        if self.worker.is_some() && self.accepting_audio.swap(false, Ordering::AcqRel) {
            // If the caller forgot to request shutdown, request it here. A
            // graceful join can wait on a full output queue, so keep draining
            // events from the consumer thread before calling this method.
            let _ = self.control_tx.send(ControlCommand::Shutdown);
        }
        if let Some(worker) = self.worker.take() {
            worker
                .join()
                .map_err(|_| AsrError::WorkerThread("worker panicked".to_owned()))?;
        }
        Ok(())
    }
}

impl Drop for AsrRuntime {
    fn drop(&mut self) {
        // Destruction is best-effort and cancellation is cooperative: an in-flight
        // native inference call cannot be interrupted safely from another thread.
        self.stopping.store(true, Ordering::Release);
        self.accepting_audio.store(false, Ordering::Release);
        let _ = self.control_tx.try_send(ControlCommand::Shutdown);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}
