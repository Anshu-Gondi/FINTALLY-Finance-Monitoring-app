//! FinTally Voice runtime engine.
//!
//! This module owns and initializes the C voice pipeline, its backing memory,
//! and its lifecycle. All calls that mutate the C pipeline are serialized
//! through `&mut self`, matching the C API's single-owner-thread contract.
//!
//! The engine is deliberately `!Send + !Sync`: construct it on the thread that
//! owns audio/DSP processing. Cross-thread communication should use owned Rust
//! messages in `threads/`, never share the native pipeline's raw pointers.
//!
//! Memory notes:
//! - Four long-lived buffers use 64-byte-aligned allocations.
//! - The default dynamic + active-codebook working-set estimate is capped at
//!   100 KiB (102_400 bytes); ASR model memory is budgeted separately.
//! - 64-byte alignment is a placement aid, not a guarantee that all data stays
//!   resident in L1/L2 cache on every target.
//! - No heap allocation occurs in `process_pcm16_frame` or `try_pop_event`.

mod events;
mod types;
use events::decode_event_kind;
use types::FEATURE_DIMENSION;
pub use types::*;
#[cfg(test)]
use types::{MAX_DSP_WORKING_SET_BYTES, MAX_EVENT_CAPACITY};

use core::alloc::Layout;
use core::convert::TryFrom;
use core::marker::PhantomPinned;
use core::mem::{size_of, MaybeUninit};
use core::pin::Pin;
use core::ptr::NonNull;
use std::alloc::{alloc_zeroed, dealloc};
use std::ffi::CString;
use std::marker::PhantomData;
use std::rc::Rc;

use crate::ffi::{self, AbiCheckError, NativeAbiInfo};

const CACHE_LINE_BYTES: usize = 64;

/// A native allocation whose base pointer satisfies the requested alignment.
struct AlignedBuffer {
    ptr: NonNull<u8>,
    len: usize,
    layout: Layout,
}

impl AlignedBuffer {
    fn new_zeroed(
        len: usize,
        alignment: usize,
        component: &'static str,
    ) -> Result<Self, EngineError> {
        if len == 0 {
            return Err(EngineError::InvalidConfiguration(
                "native workspace/storage size must be non-zero",
            ));
        }
        let layout = Layout::from_size_align(len, alignment)
            .map_err(|_| EngineError::SizeOverflow("aligned allocation layout"))?;
        // SAFETY: `layout` is valid and non-zero. The returned pointer is freed
        // with the same layout in `Drop`.
        let allocation = unsafe { alloc_zeroed(layout) };
        let ptr = NonNull::new(allocation).ok_or(EngineError::AllocationFailed {
            component,
            bytes: len,
        })?;
        Ok(Self { ptr, len, layout })
    }

    fn as_mut_ptr(&mut self) -> *mut u8 {
        self.ptr.as_ptr()
    }

    fn len(&self) -> usize {
        self.len
    }
}

impl Drop for AlignedBuffer {
    fn drop(&mut self) {
        // SAFETY: `ptr` came from `alloc_zeroed(self.layout)` and is deallocated
        // exactly once with its original layout.
        unsafe { dealloc(self.ptr.as_ptr(), self.layout) };
    }
}

/// Native structs are pinned because `FinVoicePipeline` stores pointers to the
/// DSP, language FSM, event queue, and text builder inside this allocation.
struct NativeContexts {
    dsp: MaybeUninit<ffi::FinVoiceDspContext>,
    language_fsm: MaybeUninit<ffi::FinVoiceLanguageFsm>,
    event_queue: MaybeUninit<ffi::FinVoiceEventQueue>,
    text_builder: MaybeUninit<ffi::FinVoiceTextBuilder>,
    pipeline: MaybeUninit<ffi::FinVoicePipeline>,
    _pin: PhantomPinned,
}

impl NativeContexts {
    fn uninitialized() -> Self {
        Self {
            dsp: MaybeUninit::uninit(),
            language_fsm: MaybeUninit::uninit(),
            event_queue: MaybeUninit::uninit(),
            text_builder: MaybeUninit::uninit(),
            pipeline: MaybeUninit::uninit(),
            _pin: PhantomPinned,
        }
    }
}

/// Owns the C DSP pipeline and all backing allocations.
///
/// Construct this on the eventual audio/DSP owner thread. The C pipeline API
/// requires mutating calls to be serialized on one logical owner thread.
pub struct VoiceEngine {
    config: EngineConfig,
    state: EngineState,
    native_abi: NativeAbiInfo,
    memory_report: EngineMemoryReport,
    stats: EngineStats,
    contexts: Pin<Box<NativeContexts>>,
    _dsp_workspace: AlignedBuffer,
    _pipeline_workspace: AlignedBuffer,
    _event_storage: AlignedBuffer,
    _text_storage: AlignedBuffer,
    // `Rc` is neither Send nor Sync. This deliberately prevents sharing/moving
    // this native-owner object across threads; send owned events instead.
    _owner_thread_only: PhantomData<Rc<()>>,
}

impl VoiceEngine {
    /// Build and initialize the native DSP pipeline. No audio is processed yet.
    pub fn new(config: EngineConfig) -> Result<Self, EngineError> {
        let config = config.validate()?;
        let native_abi = ffi::verify_native_abi().map_err(EngineError::Abi)?;
        verify_component_abis()?;
        validate_audio_contract(&native_abi)?;

        let mut dsp_config = MaybeUninit::<ffi::FinVoiceDspConfig>::zeroed();
        // SAFETY: the C default-config function initializes the complete config.
        unsafe { ffi::fin_voice_dsp_default_config(dsp_config.as_mut_ptr()) };
        // SAFETY: successful completion of the default-config call initializes it.
        let mut dsp_config = unsafe { dsp_config.assume_init() };
        dsp_config.sample_rate_hz = native_abi.sample_rate_hz;
        dsp_config.channels = 1;
        dsp_config.frame_samples = u16::try_from(native_abi.frame_samples)
            .map_err(|_| EngineError::InvalidConfiguration("frame size does not fit u16"))?;
        dsp_config.hop_samples = u16::try_from(native_abi.hop_samples)
            .map_err(|_| EngineError::InvalidConfiguration("hop size does not fit u16"))?;
        dsp_config.simd_backend = ffi::FinVoiceSimdBackend_FIN_SIMD_AUTO;

        // SAFETY: `dsp_config` is a fully initialized value and lives through the call.
        let config_status = unsafe { ffi::fin_voice_dsp_validate_config(&dsp_config) };
        ensure_native_status("DSP configuration validation", config_status)?;

        // Workspace-size queries do not allocate and are performed exactly once.
        // SAFETY: `dsp_config` points to a valid configuration.
        let dsp_workspace_bytes = unsafe { ffi::fin_voice_dsp_workspace_size(&dsp_config) };
        // The pipeline workspace size is independent of the context values.
        let pipeline_workspace_bytes = unsafe { ffi::fin_voice_pipeline_workspace_size() };
        if dsp_workspace_bytes == 0 || pipeline_workspace_bytes == 0 {
            return Err(EngineError::InvalidNativeResult(
                "native workspace-size query returned zero",
            ));
        }

        let event_storage_bytes = config
            .event_capacity
            .checked_mul(size_of::<ffi::FinVoiceEvent>())
            .ok_or(EngineError::SizeOverflow("event queue storage"))?;
        let active_centroids = [
            ffi::FIN_VOICE_CODEBOOK_EN_CENTROID_COUNT,
            ffi::FIN_VOICE_CODEBOOK_FR_CENTROID_COUNT,
            ffi::FIN_VOICE_CODEBOOK_HI_CENTROID_COUNT,
        ]
        .into_iter()
        .max()
        .unwrap_or(128) as usize;
        let active_codebook_bytes = active_centroids
            .checked_mul(FEATURE_DIMENSION)
            .and_then(|count| count.checked_mul(size_of::<f32>()))
            // Include the active codebook's normalization mean and inverse std.
            .and_then(|bytes| bytes.checked_add(2 * FEATURE_DIMENSION * size_of::<f32>()))
            .ok_or(EngineError::SizeOverflow("active codebook estimate"))?;
        let frame_transient_bytes = (native_abi.frame_samples as usize)
            .checked_mul(size_of::<i16>())
            .and_then(|bytes| bytes.checked_add(size_of::<ffi::FinVoicePipelineFrameResult>()))
            .ok_or(EngineError::SizeOverflow("frame transient estimate"))?;
        let context_bytes = size_of::<NativeContexts>();
        // Four allocations are independently aligned to 64 bytes. Reserve up
        // to 63 bytes of alignment slack for each in the accounting estimate.
        let alignment_slack_bytes = 4 * (CACHE_LINE_BYTES - 1);
        let accounted = [
            dsp_workspace_bytes,
            pipeline_workspace_bytes,
            event_storage_bytes,
            config.text_capacity_bytes,
            context_bytes,
            active_codebook_bytes,
            frame_transient_bytes,
            alignment_slack_bytes,
        ]
        .into_iter()
        .try_fold(0usize, |sum, value| sum.checked_add(value))
        .ok_or(EngineError::SizeOverflow("working set estimate"))?;

        let memory_report = EngineMemoryReport {
            dsp_workspace_bytes,
            pipeline_workspace_bytes,
            event_storage_bytes,
            text_storage_bytes: config.text_capacity_bytes,
            native_context_bytes: context_bytes,
            active_codebook_bytes,
            one_frame_transient_bytes: frame_transient_bytes,
            alignment_slack_bytes,
            total_accounted_bytes: accounted,
            budget_bytes: config.working_set_budget_bytes,
        };
        if accounted > config.working_set_budget_bytes {
            return Err(EngineError::WorkingSetExceeded {
                required_bytes: accounted,
                budget_bytes: config.working_set_budget_bytes,
            });
        }

        let mut dsp_workspace =
            AlignedBuffer::new_zeroed(dsp_workspace_bytes, CACHE_LINE_BYTES, "DSP workspace")?;
        let mut pipeline_workspace = AlignedBuffer::new_zeroed(
            pipeline_workspace_bytes,
            CACHE_LINE_BYTES,
            "pipeline workspace",
        )?;
        let mut event_storage = AlignedBuffer::new_zeroed(
            event_storage_bytes,
            CACHE_LINE_BYTES,
            "event queue storage",
        )?;
        let mut text_storage = AlignedBuffer::new_zeroed(
            config.text_capacity_bytes,
            CACHE_LINE_BYTES,
            "text builder storage",
        )?;

        // Allocate the contexts at a stable address before initializing them;
        // pipeline_init stores pointers to the component structs.
        let mut contexts = Box::pin(NativeContexts::uninitialized());
        // SAFETY: we never move `NativeContexts` after pinning it. Only its
        // MaybeUninit fields are initialized in place through these pointers.
        let slots = unsafe { contexts.as_mut().get_unchecked_mut() as *mut NativeContexts };
        let dsp_ptr = unsafe { (*slots).dsp.as_mut_ptr() };
        let fsm_ptr = unsafe { (*slots).language_fsm.as_mut_ptr() };
        let queue_ptr = unsafe { (*slots).event_queue.as_mut_ptr() };
        let text_ptr = unsafe { (*slots).text_builder.as_mut_ptr() };
        let pipeline_ptr = unsafe { (*slots).pipeline.as_mut_ptr() };

        // SAFETY: all arguments point to live, appropriately sized storage;
        // the DSP workspace is 64-byte aligned and remains owned by `Self`.
        let status = unsafe {
            ffi::fin_voice_dsp_init(
                dsp_ptr,
                &dsp_config,
                dsp_workspace.as_mut_ptr().cast(),
                dsp_workspace.len(),
            )
        };
        ensure_native_status("DSP initialization", status)?;

        let mut fsm_config = MaybeUninit::<ffi::FinVoiceLanguageFsmConfig>::zeroed();
        // SAFETY: the default-config function initializes the complete config.
        unsafe { ffi::fin_voice_language_fsm_config_default(fsm_config.as_mut_ptr()) };
        // SAFETY: initialized by the native default-config function above.
        let fsm_config = unsafe { fsm_config.assume_init() };
        // SAFETY: fsm_ptr points to stable writable storage and fsm_config is valid.
        let status = unsafe { ffi::fin_voice_language_fsm_init(fsm_ptr, &fsm_config) };
        ensure_native_status("language FSM initialization", status)?;

        // SAFETY: event_storage has enough bytes for `event_capacity` contiguous
        // FinVoiceEvent values and at least their required alignment.
        let status = unsafe {
            ffi::fin_voice_event_queue_init(
                queue_ptr,
                event_storage.as_mut_ptr().cast(),
                config.event_capacity,
            )
        };
        ensure_native_status("event queue initialization", status)?;

        // SAFETY: text_storage is writable, aligned, and remains live with Self.
        let status = unsafe {
            ffi::fin_voice_text_builder_init(
                text_ptr,
                text_storage.as_mut_ptr().cast(),
                config.text_capacity_bytes,
            )
        };
        ensure_native_status("text builder initialization", status)?;

        // SAFETY: the pipeline workspace is live and aligned. All component
        // contexts and their backing storage are initialized and remain at stable
        // addresses for the lifetime of this pinned owner.
        let status = unsafe {
            ffi::fin_voice_pipeline_init(
                pipeline_ptr,
                pipeline_workspace.as_mut_ptr().cast(),
                pipeline_workspace.len(),
                dsp_ptr,
                fsm_ptr,
                queue_ptr,
                text_ptr,
            )
        };
        ensure_native_status("voice pipeline initialization", status)?;

        Ok(Self {
            config,
            state: EngineState::Initialized,
            native_abi,
            memory_report,
            stats: EngineStats::default(),
            contexts,
            _dsp_workspace: dsp_workspace,
            _pipeline_workspace: pipeline_workspace,
            _event_storage: event_storage,
            _text_storage: text_storage,
            _owner_thread_only: PhantomData,
        })
    }

    pub fn state(&self) -> EngineState {
        self.state
    }

    pub fn config(&self) -> EngineConfig {
        self.config
    }

    pub fn native_abi(&self) -> NativeAbiInfo {
        self.native_abi
    }

    pub fn memory_report(&self) -> EngineMemoryReport {
        self.memory_report
    }

    pub fn stats(&self) -> EngineStats {
        let mut stats = self.stats;
        // SAFETY: the pinned queue context was initialized in `new` and remains live.
        stats.native_events_dropped =
            unsafe { ffi::fin_voice_event_queue_dropped_count(self.queue_ptr()) };
        stats
    }

    /// Start processing. Language selection is performed separately and must be
    /// explicit; the C core will reject frame processing while language is unknown.
    pub fn start(&mut self) -> Result<(), EngineError> {
        match self.state {
            EngineState::Initialized => {
                self.state = EngineState::Running;
                Ok(())
            }
            other => Err(EngineError::InvalidState {
                operation: "start",
                state: other,
            }),
        }
    }

    /// Stop frame processing while retaining initialized native memory.
    pub fn stop(&mut self) -> Result<(), EngineError> {
        match self.state {
            EngineState::Running => {
                self.state = EngineState::Initialized;
                Ok(())
            }
            other => Err(EngineError::InvalidState {
                operation: "stop",
                state: other,
            }),
        }
    }

    /// Mark the engine terminal. Native buffers are released when `self` drops.
    pub fn shutdown(&mut self) {
        self.state = EngineState::Stopped;
    }

    /// Select a supported language explicitly (English, French, or Hindi).
    pub fn select_language(&mut self, language_tag: &str) -> Result<(), EngineError> {
        self.require_not_stopped("select language")?;
        let c_tag = CString::new(language_tag).map_err(|_| EngineError::InvalidLanguageTag)?;
        // SAFETY: CString guarantees a valid NUL-terminated string for this call.
        let status = unsafe {
            ffi::fin_voice_pipeline_select_language_tag(self.pipeline_mut_ptr(), c_tag.as_ptr())
        };
        ensure_native_status("language selection", status)
    }

    /// Get the selected/confirmed language, or `Unknown` when none is active.
    pub fn active_language(&self) -> Result<VoiceLanguage, EngineError> {
        self.require_not_stopped("query active language")?;
        // SAFETY: pipeline pointer refers to the initialized pinned context.
        let language = unsafe { ffi::fin_voice_pipeline_active_language(self.pipeline_ptr()) };
        VoiceLanguage::from_native(language)
    }

    /// Process exactly one 20 ms PCM16 mono frame and publish its native events.
    ///
    /// Timestamp and sample position are provided by the caller; no clock is
    /// read here. The method performs no heap allocation. If publishing fails,
    /// the native API may already have published the feature event before failing
    /// to publish the second event; see `voice_pipeline.h` for that contract.
    pub fn process_pcm16_frame(
        &mut self,
        pcm16: &[i16],
        timestamp_ns: u64,
        sample_position: u64,
    ) -> Result<FrameSummary, EngineError> {
        if self.state != EngineState::Running {
            return Err(EngineError::InvalidState {
                operation: "process audio",
                state: self.state,
            });
        }
        let expected = self.native_abi.frame_samples as usize;
        if pcm16.len() != expected {
            return Err(EngineError::InvalidFrameLength {
                expected,
                actual: pcm16.len(),
            });
        }

        let mut output = MaybeUninit::<ffi::FinVoicePipelineFrameResult>::uninit();
        // SAFETY: `pcm16` is readable for `expected` i16 elements, output points
        // to writable storage, and the engine serializes all pipeline mutations.
        let status = unsafe {
            ffi::fin_voice_pipeline_process_pcm16_frame(
                self.pipeline_mut_ptr(),
                pcm16.as_ptr(),
                pcm16.len(),
                output.as_mut_ptr(),
            )
        };
        ensure_native_status("frame DSP/VQ processing", status)?;
        // SAFETY: successful processing initializes the complete result record.
        let output = unsafe { output.assume_init() };
        let language = VoiceLanguage::from_native(output.language)?;
        let quantization = output.quantization;
        if !quantization.best_distance_squared.is_finite()
            || !quantization.second_best_distance_squared.is_finite()
            || !quantization.relative_gap.is_finite()
            || quantization.best_distance_squared < 0.0
            || quantization.second_best_distance_squared < 0.0
        {
            return Err(EngineError::InvalidNativeResult(
                "VQ distances or relative gap are not finite/non-negative",
            ));
        }
        self.stats.frames_processed = self.stats.frames_processed.saturating_add(1);

        // SAFETY: output is initialized and points to a value owned by this call.
        let publish_status = unsafe {
            ffi::fin_voice_pipeline_publish_frame_events(
                self.pipeline_mut_ptr(),
                &output,
                timestamp_ns,
                sample_position,
            )
        };
        if publish_status != ffi::FinVoicePipelineStatus_FIN_VOICE_PIPELINE_OK {
            self.stats.event_publish_failures = self.stats.event_publish_failures.saturating_add(1);
            return Err(EngineError::NativeComponentFailed {
                component: "frame event publication (partial publication is possible)",
                status: publish_status,
            });
        }
        self.stats.frames_published = self.stats.frames_published.saturating_add(1);

        Ok(FrameSummary {
            language,
            best_centroid: (quantization.best_index != usize::MAX)
                .then_some(quantization.best_index),
            second_best_centroid: (quantization.second_best_index != usize::MAX)
                .then_some(quantization.second_best_index),
            best_distance_squared: quantization.best_distance_squared,
            second_best_distance_squared: quantization.second_best_distance_squared,
            relative_gap: quantization.relative_gap,
        })
    }

    /// Append UTF-8 text from a real ASR backend to the bounded native builder.
    /// Route ASR results to the engine-owner thread; do not call this concurrently
    /// with audio processing on the same native pipeline.
    pub fn append_recognized_text(&mut self, text: &str) -> Result<(), EngineError> {
        self.require_not_stopped("append recognized text")?;
        // `str` is valid UTF-8; pointer remains valid for the duration of the call.
        let status = unsafe {
            ffi::fin_voice_pipeline_append_recognized_text(
                self.pipeline_mut_ptr(),
                text.as_ptr().cast(),
                text.len(),
            )
        };
        ensure_native_status("append recognized text", status)
    }

    /// Flush recognized text into caller-owned storage without allocating.
    /// Returns the number of output bytes; the destination must be large enough.
    pub fn flush_recognized_text(&mut self, destination: &mut [u8]) -> Result<usize, EngineError> {
        self.require_not_stopped("flush recognized text")?;
        if destination.is_empty() {
            return Err(EngineError::InvalidConfiguration(
                "transcript destination must not be empty",
            ));
        }
        let mut output_len = 0usize;
        // SAFETY: destination is writable for its declared length and output_len
        // is a valid writable pointer. The C API checks capacity before writing.
        let status = unsafe {
            ffi::fin_voice_pipeline_flush_recognized_text(
                self.pipeline_mut_ptr(),
                destination.as_mut_ptr().cast(),
                destination.len(),
                &mut output_len,
            )
        };
        ensure_native_status("flush recognized text", status)?;
        if output_len > destination.len() {
            return Err(EngineError::InvalidNativeResult(
                "text builder returned a length beyond the supplied destination",
            ));
        }
        Ok(output_len)
    }

    /// Pop one event. `Ok(None)` means the queue is currently empty.
    /// This copies the event to fixed-size Rust data and performs no heap allocation.
    pub fn try_pop_event(&mut self) -> Result<Option<EngineEvent>, EngineError> {
        self.require_not_stopped("pop event")?;
        let mut event = MaybeUninit::<ffi::FinVoiceEvent>::uninit();
        // SAFETY: the queue is initialized and the output points to writable storage.
        let status =
            unsafe { ffi::fin_voice_event_queue_pop(self.queue_mut_ptr(), event.as_mut_ptr()) };
        if status == ffi::FinVoiceEventQueueStatus_FIN_VOICE_EVENT_QUEUE_ERROR_EMPTY {
            return Ok(None);
        }
        ensure_native_status("event queue pop", status)?;
        // SAFETY: a successful queue pop initializes the event record.
        let event = unsafe { event.assume_init() };
        let kind = decode_event_kind(&event)?;
        self.stats.events_popped = self.stats.events_popped.saturating_add(1);
        Ok(Some(EngineEvent {
            timestamp_ns: event.timestamp_ns,
            sample_position: event.sample_position,
            sequence_id: event.sequence_id,
            flags: event.flags,
            kind,
        }))
    }

    /// Reset the native pipeline while stopped from active processing. Language
    /// selection and queued events are cleared by the C API.
    pub fn reset(&mut self) -> Result<(), EngineError> {
        if self.state != EngineState::Initialized {
            return Err(EngineError::InvalidState {
                operation: "reset (stop first)",
                state: self.state,
            });
        }
        // `&mut self` and !Send/!Sync ensure no other Rust caller can concurrently
        // mutate/pop the queue through this engine instance.
        let status = unsafe { ffi::fin_voice_pipeline_reset(self.pipeline_mut_ptr()) };
        ensure_native_status("pipeline reset", status)?;
        self.stats = EngineStats::default();
        Ok(())
    }

    fn require_not_stopped(&self, operation: &'static str) -> Result<(), EngineError> {
        if self.state == EngineState::Stopped {
            return Err(EngineError::InvalidState {
                operation,
                state: self.state,
            });
        }
        Ok(())
    }

    fn pipeline_ptr(&self) -> *const ffi::FinVoicePipeline {
        self.contexts.as_ref().get_ref().pipeline.as_ptr()
    }

    fn pipeline_mut_ptr(&mut self) -> *mut ffi::FinVoicePipeline {
        // SAFETY: the NativeContexts allocation is pinned for the engine lifetime.
        unsafe {
            self.contexts
                .as_mut()
                .get_unchecked_mut()
                .pipeline
                .as_mut_ptr()
        }
    }

    fn queue_ptr(&self) -> *const ffi::FinVoiceEventQueue {
        self.contexts.as_ref().get_ref().event_queue.as_ptr()
    }

    fn queue_mut_ptr(&mut self) -> *mut ffi::FinVoiceEventQueue {
        // SAFETY: the NativeContexts allocation is pinned for the engine lifetime.
        unsafe {
            self.contexts
                .as_mut()
                .get_unchecked_mut()
                .event_queue
                .as_mut_ptr()
        }
    }
}

fn validate_audio_contract(abi: &NativeAbiInfo) -> Result<(), EngineError> {
    let checks = [
        (
            "sample rate",
            ffi::FIN_VOICE_SAMPLE_RATE_HZ,
            abi.sample_rate_hz,
        ),
        (
            "frame size",
            ffi::FIN_VOICE_FRAME_SAMPLES,
            abi.frame_samples,
        ),
        ("hop size", ffi::FIN_VOICE_HOP_SAMPLES, abi.hop_samples),
        (
            "feature dimension",
            ffi::FIN_VOICE_FEATURE_DIM,
            abi.feature_dimension,
        ),
    ];
    for (field, expected, actual) in checks {
        if actual != expected {
            return Err(EngineError::Abi(AbiCheckError::ValueMismatch {
                field,
                expected,
                actual,
            }));
        }
    }
    Ok(())
}

fn verify_component_abis() -> Result<(), EngineError> {
    let versions = [
        (
            "pipeline ABI version",
            unsafe { ffi::fin_voice_pipeline_abi_version() },
            (ffi::FIN_VOICE_PIPELINE_ABI_MAJOR << 16) | ffi::FIN_VOICE_PIPELINE_ABI_MINOR,
        ),
        (
            "event ABI version",
            unsafe { ffi::fin_voice_event_abi_version() },
            (ffi::FIN_VOICE_EVENT_ABI_MAJOR << 16) | ffi::FIN_VOICE_EVENT_ABI_MINOR,
        ),
        (
            "language FSM ABI version",
            unsafe { ffi::fin_voice_language_fsm_abi_version() },
            (ffi::FIN_VOICE_LANGUAGE_FSM_ABI_MAJOR << 16) | ffi::FIN_VOICE_LANGUAGE_FSM_ABI_MINOR,
        ),
        (
            "text builder ABI version",
            unsafe { ffi::fin_voice_text_builder_abi_version() },
            (ffi::FIN_VOICE_TEXT_BUILDER_ABI_MAJOR << 16) | ffi::FIN_VOICE_TEXT_BUILDER_ABI_MINOR,
        ),
    ];
    for (field, actual, expected) in versions {
        if actual != expected {
            return Err(EngineError::Abi(AbiCheckError::ValueMismatch {
                field,
                expected,
                actual,
            }));
        }
    }
    Ok(())
}

fn ensure_native_status(component: &'static str, status: u32) -> Result<(), EngineError> {
    if status == 0 {
        Ok(())
    } else {
        Err(EngineError::NativeComponentFailed { component, status })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_stay_inside_declared_working_set_ceiling() {
        let config = EngineConfig::default();
        assert!(config.validate().is_ok());
        assert!(config.working_set_budget_bytes <= MAX_DSP_WORKING_SET_BYTES);
    }

    #[test]
    fn rejects_unbounded_or_zero_queue_configuration() {
        assert!(EngineConfig {
            event_capacity: 0,
            ..EngineConfig::default()
        }
        .validate()
        .is_err());
        assert!(EngineConfig {
            event_capacity: MAX_EVENT_CAPACITY + 1,
            ..EngineConfig::default()
        }
        .validate()
        .is_err());
    }

    #[test]
    fn rejects_working_set_budget_above_100_kib() {
        assert!(EngineConfig {
            working_set_budget_bytes: MAX_DSP_WORKING_SET_BYTES + 1,
            ..EngineConfig::default()
        }
        .validate()
        .is_err());
    }

    #[test]
    fn fixed_text_is_utf8_without_heap_allocation() {
        let mut bytes = [0u8; ffi::FIN_VOICE_EVENT_TEXT_CAPACITY as usize];
        bytes[..5].copy_from_slice(b"hello");
        let text = FixedText { bytes, len: 5 };
        assert_eq!(text.as_str(), "hello");
        assert_eq!(text.len(), 5);
    }
}
