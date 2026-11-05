//! CPU affinity and worker-startup helpers for FinTally Voice.
//!
//! Pin only the two CPU-bound workers (normally DSP and ASR). Audio capture,
//! UI/event bridging, and LLM/network coordination should remain unpinned.
//! This module does not restrict the process or other threads to two CPUs; use
//! an OS cpuset/container CPU set as well when a process-wide two-CPU limit is
//! required.
//!
//! `core_affinity` identifies OS-reported logical CPU IDs. Two distinct IDs are
//! not guaranteed to represent two different physical cores on SMT/hybrid CPUs.
//! Inspect target topology and use `from_requested_ids` to select explicitly.

use std::error::Error;
use std::fmt;
use std::io;
use std::sync::mpsc::sync_channel;
use std::thread::{self, JoinHandle};

const CPU_WORKER_COUNT: usize = 2;

/// The only worker roles allowed to request hard CPU affinity.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(usize)]
pub enum CpuWorkerSlot {
    /// Real-time DSP / feature extraction consumer.
    Dsp = 0,
    /// CPU-heavy ASR inference worker. Configure the ASR runtime's own thread
    /// pool separately to avoid oversubscription.
    Asr = 1,
}

impl CpuWorkerSlot {
    const fn index(self) -> usize {
        self as usize
    }
}

/// Two distinct OS-reported CPU IDs selected for CPU-bound worker threads.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CpuAffinityPlan {
    cpu_ids: [usize; CPU_WORKER_COUNT],
}

impl CpuAffinityPlan {
    /// Select the first two allowed logical CPU IDs in ascending order.
    ///
    /// This is deterministic but not topology-aware. On hybrid or SMT systems,
    /// explicitly select IDs with [`Self::from_requested_ids`] after checking
    /// the system topology if physical-core separation matters.
    pub fn discover() -> Result<Self, AffinityError> {
        let mut available = available_cpu_ids()?;
        available.sort_unstable();
        available.dedup();

        if available.len() < CPU_WORKER_COUNT {
            return Err(AffinityError::InsufficientCpuIds {
                found: available.len(),
                required: CPU_WORKER_COUNT,
            });
        }

        Ok(Self {
            cpu_ids: [available[0], available[1]],
        })
    }

    /// Use explicitly selected CPU IDs after verifying both are currently
    /// allowed to this process and are distinct.
    pub fn from_requested_ids(cpu_ids: [usize; CPU_WORKER_COUNT]) -> Result<Self, AffinityError> {
        if cpu_ids[0] == cpu_ids[1] {
            return Err(AffinityError::DuplicateCpuIds(cpu_ids[0]));
        }

        let available = available_cpu_ids()?;
        for cpu_id in cpu_ids {
            if !available.contains(&cpu_id) {
                return Err(AffinityError::CpuIdNotAllowed(cpu_id));
            }
        }

        Ok(Self { cpu_ids })
    }

    /// Return the configured OS CPU ID for a CPU-bound role.
    pub const fn cpu_id(self, slot: CpuWorkerSlot) -> usize {
        self.cpu_ids[slot.index()]
    }

    /// Pin the calling OS thread. Invoke this at the beginning of the worker,
    /// before it initializes an ASR runtime or enters its processing loop.
    pub fn pin_current(self, slot: CpuWorkerSlot) -> Result<(), AffinityError> {
        let cpu_id = self.cpu_id(slot);

        #[cfg(any(
            target_os = "linux",
            target_os = "windows",
            target_os = "macos",
            target_os = "android",
            target_os = "freebsd",
            target_os = "netbsd"
        ))]
        {
            if core_affinity::set_for_current(core_affinity::CoreId { id: cpu_id }) {
                Ok(())
            } else {
                Err(AffinityError::PinFailed(cpu_id))
            }
        }

        #[cfg(not(any(
            target_os = "linux",
            target_os = "windows",
            target_os = "macos",
            target_os = "android",
            target_os = "freebsd",
            target_os = "netbsd"
        )))]
        {
            let _ = cpu_id;
            Err(AffinityError::UnsupportedPlatform)
        }
    }

    /// Spawn a named CPU worker and do not return until its affinity attempt has
    /// succeeded. If pinning fails, the task is never run and the thread is
    /// joined before the error is returned.
    pub fn spawn_pinned_cpu_worker<F>(
        self,
        slot: CpuWorkerSlot,
        name: &'static str,
        task: F,
    ) -> Result<JoinHandle<()>, WorkerStartError>
    where
        F: FnOnce() + Send + 'static,
    {
        let (ready_tx, ready_rx) = sync_channel::<Result<(), AffinityError>>(1);
        let plan = self;

        let handle = thread::Builder::new()
            .name(name.to_owned())
            .spawn(move || match plan.pin_current(slot) {
                Ok(()) => {
                    // The receiver is waiting during worker startup, so this
                    // bounded send cannot accumulate messages or block on a
                    // full queue.
                    if ready_tx.send(Ok(())).is_ok() {
                        task();
                    }
                }
                Err(error) => {
                    let _ = ready_tx.send(Err(error));
                }
            })
            .map_err(WorkerStartError::ThreadSpawn)?;

        match ready_rx.recv() {
            Ok(Ok(())) => Ok(handle),
            Ok(Err(error)) => {
                let _ = handle.join();
                Err(WorkerStartError::Affinity(error))
            }
            Err(_) => {
                let _ = handle.join();
                Err(WorkerStartError::StartupHandshakeClosed)
            }
        }
    }
}

/// Spawn an unpinned service thread for UI/event handling or LLM coordination.
/// Keep CPU-heavy work out of these roles; they must not be called by the audio
/// callback or the DSP hot path.
pub fn spawn_unpinned_service_worker<F>(name: &'static str, task: F) -> io::Result<JoinHandle<()>>
where
    F: FnOnce() + Send + 'static,
{
    thread::Builder::new().name(name.to_owned()).spawn(task)
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AffinityError {
    UnsupportedPlatform,
    CoreDiscoveryFailed,
    InsufficientCpuIds { found: usize, required: usize },
    DuplicateCpuIds(usize),
    CpuIdNotAllowed(usize),
    PinFailed(usize),
}

impl fmt::Display for AffinityError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnsupportedPlatform => {
                write!(f, "CPU affinity is not implemented for this target")
            }
            Self::CoreDiscoveryFailed => write!(f, "could not discover allowed CPU IDs"),
            Self::InsufficientCpuIds { found, required } => write!(
                f,
                "need {required} distinct allowed CPU IDs for pinned workers, found {found}"
            ),
            Self::DuplicateCpuIds(id) => write!(f, "CPU ID {id} was selected for both workers"),
            Self::CpuIdNotAllowed(id) => write!(f, "CPU ID {id} is not allowed for this process"),
            Self::PinFailed(id) => write!(f, "OS rejected affinity to CPU ID {id}"),
        }
    }
}

impl Error for AffinityError {}

#[derive(Debug)]
pub enum WorkerStartError {
    ThreadSpawn(io::Error),
    Affinity(AffinityError),
    StartupHandshakeClosed,
}

impl fmt::Display for WorkerStartError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::ThreadSpawn(error) => write!(f, "could not spawn worker thread: {error}"),
            Self::Affinity(error) => write!(f, "CPU worker affinity setup failed: {error}"),
            Self::StartupHandshakeClosed => {
                write!(f, "CPU worker exited before reporting affinity status")
            }
        }
    }
}

impl Error for WorkerStartError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            Self::ThreadSpawn(error) => Some(error),
            Self::Affinity(error) => Some(error),
            Self::StartupHandshakeClosed => None,
        }
    }
}

fn available_cpu_ids() -> Result<Vec<usize>, AffinityError> {
    #[cfg(any(
        target_os = "linux",
        target_os = "windows",
        target_os = "macos",
        target_os = "android",
        target_os = "freebsd",
        target_os = "netbsd"
    ))]
    {
        let cores = core_affinity::get_core_ids().ok_or(AffinityError::CoreDiscoveryFailed)?;
        Ok(cores.into_iter().map(|core| core.id).collect())
    }

    #[cfg(not(any(
        target_os = "linux",
        target_os = "windows",
        target_os = "macos",
        target_os = "android",
        target_os = "freebsd",
        target_os = "netbsd"
    )))]
    {
        Err(AffinityError::UnsupportedPlatform)
    }
}
