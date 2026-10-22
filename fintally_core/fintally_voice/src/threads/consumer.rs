//! Single-owner DSP consumer for the PCM-frame ingress queue.
//!
//! Construct `VoiceEngine` inside the DSP worker that runs this consumer. The
//! engine is intentionally `!Send + !Sync`; never construct it on one thread and
//! move its native context to another thread. The existing native C queue carries
//! detailed `EngineEvent` payloads (features, quantization, text, status).

use core::fmt;
use core::hint::spin_loop;
use std::thread;
use std::thread::JoinHandle;

use crate::engine::{
    EngineConfig, EngineError, EngineEvent, EngineEventKind, EngineState, VoiceEngine,
};

use super::affinity::{CpuAffinityPlan, CpuWorkerSlot, WorkerStartError};
use super::producer::{
    constrain_engine_config_to_queue, AudioBudgetError, PcmFrame, DEFAULT_TOTAL_WORKING_SET_BYTES,
};
use super::spsc::SpscConsumer;
use super::DEFAULT_AUDIO_FRAME_QUEUE_CAPACITY;

const SPINS_BEFORE_YIELD: usize = 64;

pub struct AudioFrameConsumer<const N: usize = DEFAULT_AUDIO_FRAME_QUEUE_CAPACITY> {
    queue: SpscConsumer<PcmFrame, N>,
    stats: DspConsumerStats,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct DspConsumerStats {
    pub frames_processed: u64,
    pub events_forwarded: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DspConsumerExit {
    /// The capture/producer endpoint closed and all queued frames were processed.
    InputClosed,
    /// The native pipeline emitted its explicit shutdown event.
    NativeShutdown,
}

#[derive(Debug)]
pub enum DspConsumerError<E> {
    Engine(EngineError),
    EventHandler(E),
}

impl<E: fmt::Display> fmt::Display for DspConsumerError<E> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Engine(error) => write!(f, "DSP consumer engine failed: {error}"),
            Self::EventHandler(error) => write!(f, "DSP consumer event handler failed: {error}"),
        }
    }
}

impl<E: std::error::Error + 'static> std::error::Error for DspConsumerError<E> {}

impl<const N: usize> AudioFrameConsumer<N> {
    pub(super) fn from_queue(queue: SpscConsumer<PcmFrame, N>) -> Self {
        Self {
            queue,
            stats: DspConsumerStats::default(),
        }
    }

    /// One non-blocking receive attempt. The frame is copied out of the fixed
    /// ring; no heap allocation occurs.
    #[inline]
    pub fn try_recv(&mut self) -> Option<PcmFrame> {
        self.queue.try_pop()
    }

    pub fn pending_approx(&self) -> usize {
        self.queue.len_approx()
    }

    pub const fn capacity(&self) -> usize {
        self.queue.capacity()
    }

    pub fn storage_bytes_estimate(&self) -> usize {
        self.queue.storage_bytes_estimate()
    }

    pub fn stats(&self) -> DspConsumerStats {
        self.stats
    }

    /// Lower the native engine's own budget so that the engine estimate plus
    /// this ingress ring remains under one combined runtime budget.
    pub fn constrain_engine_config(
        &self,
        config: EngineConfig,
        total_budget_bytes: usize,
    ) -> Result<EngineConfig, AudioBudgetError> {
        constrain_engine_config_to_queue(self, config, total_budget_bytes)
    }

    /// Run the DSP owner loop. `engine` must already be initialized, have an
    /// explicit supported language selected, and be in `Running` state.
    ///
    /// `handle_event` receives one decoded native event at a time. Keep it
    /// bounded and non-blocking; use a separate bounded downstream queue for ASR,
    /// UI, or LLM work. If it fails, the error is returned and input closes.
    pub fn run<F, E>(
        &mut self,
        engine: &mut VoiceEngine,
        mut handle_event: F,
    ) -> Result<DspConsumerExit, DspConsumerError<E>>
    where
        F: FnMut(EngineEvent) -> Result<(), E>,
    {
        if engine.state() != EngineState::Running {
            return Err(DspConsumerError::Engine(EngineError::InvalidState {
                operation: "run DSP consumer",
                state: engine.state(),
            }));
        }

        let mut idle_spins = 0usize;
        loop {
            if let Some(frame) = self.queue.try_pop() {
                idle_spins = 0;
                if let Err(error) = engine.process_pcm16_frame(
                    &frame.samples,
                    frame.timestamp_ns,
                    frame.sample_position,
                ) {
                    self.queue.close();
                    return Err(DspConsumerError::Engine(error));
                }
                self.stats.frames_processed = self.stats.frames_processed.saturating_add(1);

                loop {
                    let event = match engine.try_pop_event() {
                        Ok(Some(event)) => event,
                        Ok(None) => break,
                        Err(error) => {
                            self.queue.close();
                            return Err(DspConsumerError::Engine(error));
                        }
                    };

                    let is_shutdown = matches!(event.kind, EngineEventKind::Shutdown);
                    if let Err(error) = handle_event(event) {
                        self.queue.close();
                        return Err(DspConsumerError::EventHandler(error));
                    }
                    self.stats.events_forwarded = self.stats.events_forwarded.saturating_add(1);

                    if is_shutdown {
                        self.queue.close();
                        engine.shutdown();
                        return Ok(DspConsumerExit::NativeShutdown);
                    }
                }
                continue;
            }

            if self.queue.is_closed() {
                if engine.state() == EngineState::Running {
                    engine.stop().map_err(DspConsumerError::Engine)?;
                }
                return Ok(DspConsumerExit::InputClosed);
            }

            // Short spin improves wake-up latency for normal 20 ms frames; yield
            // after the bound so an idle worker does not monopolize a core.
            if idle_spins < SPINS_BEFORE_YIELD {
                spin_loop();
                idle_spins += 1;
            } else {
                thread::yield_now();
                idle_spins = 0;
            }
        }
    }
}

/// Startup failures that occur before the pinned DSP worker begins processing.
#[derive(Debug)]
pub enum DspWorkerSpawnError {
    WorkingSet(AudioBudgetError),
    WorkerStart(WorkerStartError),
}

impl fmt::Display for DspWorkerSpawnError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::WorkingSet(error) => write!(f, "DSP worker memory budget rejected: {error}"),
            Self::WorkerStart(error) => write!(f, "DSP worker startup failed: {error}"),
        }
    }
}

impl std::error::Error for DspWorkerSpawnError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::WorkingSet(error) => Some(error),
            Self::WorkerStart(error) => Some(error),
        }
    }
}

/// Spawn the DSP consumer on the configured CPU slot and construct `VoiceEngine`
/// inside that thread, respecting the engine's `!Send + !Sync` owner contract.
///
/// `handle_event` must forward native events quickly to a bounded downstream
/// stage; `handle_fault` receives initialization/runtime failures. Error strings
/// are allocated only on the exceptional path, never for frame/event processing.
pub fn spawn_dsp_consumer_pinned<const N: usize, F, G>(
    plan: CpuAffinityPlan,
    name: &'static str,
    mut consumer: AudioFrameConsumer<N>,
    engine_config: EngineConfig,
    language_tag: String,
    mut handle_event: F,
    mut handle_fault: G,
) -> Result<JoinHandle<()>, DspWorkerSpawnError>
where
    F: FnMut(EngineEvent) + Send + 'static,
    G: FnMut(String) + Send + 'static,
{
    // Reserve queue storage + allocation slack before VoiceEngine allocates its
    // own DSP/pipeline buffers, codebook workspace and native event storage.
    let engine_config = consumer
        .constrain_engine_config(engine_config, DEFAULT_TOTAL_WORKING_SET_BYTES)
        .map_err(DspWorkerSpawnError::WorkingSet)?;

    plan.spawn_pinned_cpu_worker(CpuWorkerSlot::Dsp, name, move || {
        // Critical ownership rule: construct and destroy VoiceEngine on this
        // worker. It must never be created on the caller and moved into the loop.
        let result: Result<(), EngineError> = (|| {
            let mut engine = VoiceEngine::new(engine_config)?;
            engine.select_language(&language_tag)?;
            engine.start()?;

            let consumer_result = consumer.run(&mut engine, |event| {
                handle_event(event);
                Ok::<(), core::convert::Infallible>(())
            });

            match consumer_result {
                Ok(_exit) => Ok(()),
                Err(DspConsumerError::Engine(error)) => Err(error),
                Err(DspConsumerError::EventHandler(never)) => match never {},
            }
        })();

        if let Err(error) = result {
            handle_fault(error.to_string());
        }
    })
    .map_err(DspWorkerSpawnError::WorkerStart)
}
