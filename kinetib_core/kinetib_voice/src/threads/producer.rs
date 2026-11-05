//! Audio-frame producer endpoint.
//!
//! Capture/resampling code publishes complete 20 ms mono PCM16 frames here. It
//! must provide monotonic timestamps and stream-relative sample positions; this
//! module does not read clocks, own an audio device, or allocate per frame.

use core::fmt;

use super::consumer::AudioFrameConsumer;
pub use super::spsc::SpscConfigError;
use super::spsc::{bounded_spsc, SpscProducer, SpscPushError};
use super::DEFAULT_AUDIO_FRAME_QUEUE_CAPACITY;

/// Canonical runtime frame: 16 kHz * 20 ms, mono PCM16 = 320 samples.
pub const PCM_FRAME_SAMPLES: usize = 320;

/// Low-latency default: 4 x 20 ms = 80 ms of buffering. Increase to 8 only if
/// measured capture/scheduler jitter justifies the additional backlog and RAM.
pub const DEFAULT_TOTAL_WORKING_SET_BYTES: usize = 100 * 1024;
const EXTERNAL_ALLOCATION_SLACK_BYTES: usize = 1024;

/// One pre-sized audio frame. The 64-byte alignment helps the queue place each
/// slot on a cache-line boundary; it does not imply cache residency.
///
/// This type is Rust-only and must not be reinterpreted as a C ABI structure.
#[repr(align(64))]
#[derive(Clone, Copy)]
pub struct PcmFrame {
    /// Monotonic nanoseconds from the capture backend, if available.
    pub timestamp_ns: u64,
    /// Sample index of `samples[0]` within the continuous input stream.
    pub sample_position: u64,
    /// Fixed-size PCM16 mono frame.
    pub samples: [i16; PCM_FRAME_SAMPLES],
}

impl fmt::Debug for PcmFrame {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Do not dump microphone contents into logs.
        f.debug_struct("PcmFrame")
            .field("timestamp_ns", &self.timestamp_ns)
            .field("sample_position", &self.sample_position)
            .field("sample_count", &PCM_FRAME_SAMPLES)
            .finish()
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct AudioProducerStats {
    pub frames_published: u64,
    pub queue_full: u64,
    pub channel_closed: u64,
    pub invalid_frame_length: u64,
}

/// Publish failure. A full/closed queue returns ownership of the exact frame,
/// letting the caller retry, count a discontinuity, or apply an explicit drop
/// policy rather than losing captured audio silently.
#[derive(Debug)]
pub enum AudioPublishError {
    InvalidFrameLength { expected: usize, actual: usize },
    QueueFull(PcmFrame),
    ChannelClosed(PcmFrame),
}

impl AudioPublishError {
    pub fn into_frame(self) -> Option<PcmFrame> {
        match self {
            Self::QueueFull(frame) | Self::ChannelClosed(frame) => Some(frame),
            Self::InvalidFrameLength { .. } => None,
        }
    }
}

impl fmt::Display for AudioPublishError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidFrameLength { expected, actual } => {
                write!(f, "expected {expected} PCM16 samples, received {actual}")
            }
            Self::QueueFull(_) => f.write_str("audio frame queue is full"),
            Self::ChannelClosed(_) => f.write_str("audio frame queue is closed"),
        }
    }
}

impl std::error::Error for AudioPublishError {}

/// Single-owner sender. `try_publish_samples` performs one bounded copy and
/// never blocks; all storage is allocated when `audio_frame_channel` is built.
pub struct AudioFrameProducer<const N: usize = DEFAULT_AUDIO_FRAME_QUEUE_CAPACITY> {
    queue: SpscProducer<PcmFrame, N>,
    stats: AudioProducerStats,
}

impl<const N: usize> AudioFrameProducer<N> {
    /// Copy one complete PCM16 frame into the preallocated ring.
    #[inline]
    pub fn try_publish_samples(
        &mut self,
        samples: &[i16],
        timestamp_ns: u64,
        sample_position: u64,
    ) -> Result<(), AudioPublishError> {
        if samples.len() != PCM_FRAME_SAMPLES {
            self.stats.invalid_frame_length = self.stats.invalid_frame_length.saturating_add(1);
            return Err(AudioPublishError::InvalidFrameLength {
                expected: PCM_FRAME_SAMPLES,
                actual: samples.len(),
            });
        }

        let mut frame_samples = [0_i16; PCM_FRAME_SAMPLES];
        frame_samples.copy_from_slice(samples);
        self.try_send_frame(PcmFrame {
            timestamp_ns,
            sample_position,
            samples: frame_samples,
        })
    }

    /// Retry or publish an already-owned frame without copying its samples.
    #[inline]
    pub fn try_send_frame(&mut self, frame: PcmFrame) -> Result<(), AudioPublishError> {
        match self.queue.try_push(frame) {
            Ok(()) => {
                self.stats.frames_published = self.stats.frames_published.saturating_add(1);
                Ok(())
            }
            Err(SpscPushError::Full(frame)) => {
                self.stats.queue_full = self.stats.queue_full.saturating_add(1);
                Err(AudioPublishError::QueueFull(frame))
            }
            Err(SpscPushError::Closed(frame)) => {
                self.stats.channel_closed = self.stats.channel_closed.saturating_add(1);
                Err(AudioPublishError::ChannelClosed(frame))
            }
        }
    }

    /// Stop accepting frames. Items already published remain readable.
    pub fn close(&mut self) {
        self.queue.close();
    }

    pub fn stats(&self) -> AudioProducerStats {
        self.stats
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
}

/// Construct the PCM-frame SPSC channel. `N` must be a power of two and at
/// least 2. Capacity is the number of usable frames (no sentinel slot).
pub fn audio_frame_channel<const N: usize>(
) -> Result<(AudioFrameProducer<N>, AudioFrameConsumer<N>), SpscConfigError> {
    let (producer, consumer) = bounded_spsc::<PcmFrame, N>()?;
    Ok((
        AudioFrameProducer {
            queue: producer,
            stats: AudioProducerStats::default(),
        },
        AudioFrameConsumer::from_queue(consumer),
    ))
}

/// Default bounded audio ingress channel.
pub fn default_audio_frame_channel() -> Result<
    (
        AudioFrameProducer<DEFAULT_AUDIO_FRAME_QUEUE_CAPACITY>,
        AudioFrameConsumer<DEFAULT_AUDIO_FRAME_QUEUE_CAPACITY>,
    ),
    SpscConfigError,
> {
    audio_frame_channel::<DEFAULT_AUDIO_FRAME_QUEUE_CAPACITY>()
}

/// Account for the frame ring before creating `VoiceEngine` so its own native
/// memory budget plus this queue stays beneath `total_budget_bytes`. The 1 KiB
/// slack covers the queue's Arc/allocator metadata conservatively; OS/audio
/// backend buffers and ASR model weights must be budgeted separately.
pub(crate) fn constrain_engine_config_to_queue<const N: usize>(
    consumer: &AudioFrameConsumer<N>,
    mut config: crate::engine::EngineConfig,
    total_budget_bytes: usize,
) -> Result<crate::engine::EngineConfig, AudioBudgetError> {
    if config.working_set_budget_bytes == 0 {
        return Err(AudioBudgetError::ZeroEngineBudget);
    }
    let queue_and_slack = consumer
        .storage_bytes_estimate()
        .checked_add(EXTERNAL_ALLOCATION_SLACK_BYTES)
        .ok_or(AudioBudgetError::SizeOverflow)?;
    let remaining = total_budget_bytes
        .checked_sub(queue_and_slack)
        .filter(|bytes| *bytes > 0)
        .ok_or(AudioBudgetError::QueueExceedsBudget {
            queue_bytes_with_slack: queue_and_slack,
            total_budget_bytes,
        })?;
    config.working_set_budget_bytes = config.working_set_budget_bytes.min(remaining);
    Ok(config)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AudioBudgetError {
    ZeroEngineBudget,
    SizeOverflow,
    QueueExceedsBudget {
        queue_bytes_with_slack: usize,
        total_budget_bytes: usize,
    },
}

impl fmt::Display for AudioBudgetError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::ZeroEngineBudget => f.write_str("engine working-set budget must be non-zero"),
            Self::SizeOverflow => f.write_str("audio queue memory estimate overflowed usize"),
            Self::QueueExceedsBudget { queue_bytes_with_slack, total_budget_bytes } => write!(
                f,
                "audio ingress queue needs {queue_bytes_with_slack} bytes including slack, over total budget {total_budget_bytes}"
            ),
        }
    }
}

impl std::error::Error for AudioBudgetError {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_wrong_frame_length_before_enqueue() {
        let (mut producer, consumer) = audio_frame_channel::<4>().unwrap();
        let error = producer.try_publish_samples(&[0; 16], 10, 0).unwrap_err();
        assert!(matches!(
            error,
            AudioPublishError::InvalidFrameLength {
                expected: 320,
                actual: 16
            }
        ));
        assert_eq!(producer.stats().invalid_frame_length, 1);
        assert_eq!(consumer.pending_approx(), 0);
    }

    #[test]
    fn queue_full_returns_frame_and_preserves_metadata() {
        let (mut producer, mut consumer) = audio_frame_channel::<2>().unwrap();
        let samples = [123_i16; PCM_FRAME_SAMPLES];
        for position in [0, 320] {
            producer
                .try_publish_samples(&samples, 99, position)
                .unwrap();
        }
        let error = producer
            .try_publish_samples(&samples, 100, 640)
            .unwrap_err();
        let returned = error.into_frame().unwrap();
        assert_eq!(returned.sample_position, 640);
        assert_eq!(returned.timestamp_ns, 100);
        assert_eq!(consumer.try_recv().unwrap().sample_position, 0);
    }

    #[test]
    fn frame_slot_is_cache_line_aligned() {
        assert_eq!(core::mem::align_of::<PcmFrame>(), 64);
        assert_eq!(core::mem::size_of::<PcmFrame>() % 64, 0);
    }
}
