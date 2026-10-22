//! Safe, narrow Rust wrapper for the FinTally Voice offline-builder C ABI.
//!
//! Add `mod ffi;` to `offline_builder/src/main.rs` (or declare it from the crate
//! root) before using this module. Keep one `Dsp` per worker: its native context
//! owns mutable scratch and must not be called concurrently.

use std::ffi::CStr;
use std::fmt;
use std::marker::PhantomData;
use std::ptr::NonNull;
use std::rc::Rc;

#[allow(
    non_camel_case_types,
    non_upper_case_globals,
    non_snake_case,
    dead_code
)]
mod raw {
    include!(concat!(env!("OUT_DIR"), "/fin_voice_builder_bindings.rs"));
}

pub const FRAME_SAMPLES: usize = raw::FIN_VOICE_BUILDER_FRAME_SAMPLES as usize;
pub const FEATURE_DIMENSION: usize = raw::FIN_VOICE_BUILDER_FEATURE_DIMENSION as usize;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DspInitError {
    AllocationFailed,
    NativeInitializationFailed(i32),
    InvalidNullHandle,
}

impl fmt::Display for DspInitError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::AllocationFailed => write!(
                formatter,
                "could not allocate the native DSP context/workspace"
            ),
            Self::NativeInitializationFailed(status) => write!(
                formatter,
                "native DSP initialization failed with C status {status}"
            ),
            Self::InvalidNullHandle => write!(
                formatter,
                "native DSP constructor returned a null handle without an error status"
            ),
        }
    }
}

impl std::error::Error for DspInitError {}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DspError {
    NativeStatus(i32),
}

impl fmt::Display for DspError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::NativeStatus(status) => {
                write!(formatter, "native DSP returned C status {status}")
            }
        }
    }
}

impl std::error::Error for DspError {}

/// A single-thread-owned DSP context. Create one instance per parallel worker.
pub struct Dsp {
    handle: NonNull<raw::FinVoiceBuilderDsp>,
    // Rc is intentionally !Send + !Sync: the underlying C context is mutable
    // and is not designed for concurrent calls using the same handle.
    _not_send_sync: PhantomData<Rc<()>>,
}

impl Dsp {
    pub fn new() -> Result<Self, DspInitError> {
        let mut status = 0_i32;
        // SAFETY: the bridge accepts a valid pointer to status and owns all
        // allocations behind its opaque handle. No Rust memory is borrowed.
        let handle = unsafe { raw::fin_voice_builder_dsp_create(&mut status) };
        let Some(handle) = NonNull::new(handle) else {
            if status == raw::FIN_VOICE_BUILDER_CREATE_STATUS_ALLOCATION_FAILURE as i32 {
                return Err(DspInitError::AllocationFailed);
            }
            if status == 0 {
                return Err(DspInitError::InvalidNullHandle);
            }
            return Err(DspInitError::NativeInitializationFailed(status));
        };

        Ok(Self {
            handle,
            _not_send_sync: PhantomData,
        })
    }

    /// Extract the canonical 32 FP32 features from exactly one 320-sample frame.
    /// The returned vector is only committed by C when extraction succeeds.
    pub fn extract_f32(
        &mut self,
        frame: &[f32; FRAME_SAMPLES],
    ) -> Result<[f32; FEATURE_DIMENSION], DspError> {
        let mut output = [0.0_f32; FEATURE_DIMENSION];
        // SAFETY: both slices have fixed, compile-time lengths and remain live
        // for the call. The bridge validates lengths and copies output on success.
        let status = unsafe {
            raw::fin_voice_builder_dsp_extract_f32(
                self.handle.as_ptr(),
                frame.as_ptr(),
                frame.len(),
                output.as_mut_ptr(),
                output.len(),
            )
        };
        if status == 0 {
            Ok(output)
        } else {
            Err(DspError::NativeStatus(status))
        }
    }

    /// PCM16 equivalent of [`Dsp::extract_f32`].
    pub fn extract_pcm16(
        &mut self,
        frame: &[i16; FRAME_SAMPLES],
    ) -> Result<[f32; FEATURE_DIMENSION], DspError> {
        let mut output = [0.0_f32; FEATURE_DIMENSION];
        // SAFETY: both slices have fixed, compile-time lengths and remain live
        // for the call. The bridge validates lengths and copies output on success.
        let status = unsafe {
            raw::fin_voice_builder_dsp_extract_pcm16(
                self.handle.as_ptr(),
                frame.as_ptr(),
                frame.len(),
                output.as_mut_ptr(),
                output.len(),
            )
        };
        if status == 0 {
            Ok(output)
        } else {
            Err(DspError::NativeStatus(status))
        }
    }

    /// Returns the C DSP's requested workspace size, useful in diagnostics.
    pub fn workspace_bytes(&self) -> usize {
        // SAFETY: handle is non-null and owned by self.
        unsafe { raw::fin_voice_builder_dsp_workspace_bytes(self.handle.as_ptr()) }
    }

    /// Returns the numeric backend ID selected once during initialization.
    pub fn active_backend_id(&self) -> i32 {
        // SAFETY: handle is non-null and owned by self.
        unsafe { raw::fin_voice_builder_dsp_active_backend(self.handle.as_ptr()) }
    }

    /// Returns the stable backend name provided by the native C implementation.
    pub fn active_backend_name(&self) -> String {
        // SAFETY: the bridge returns a static NUL-terminated string.
        let pointer = unsafe { raw::fin_voice_builder_dsp_backend_name(self.handle.as_ptr()) };
        if pointer.is_null() {
            return "unknown".to_owned();
        }
        // SAFETY: documented bridge contract says this pointer references a
        // static NUL-terminated string which remains valid after the call.
        unsafe { CStr::from_ptr(pointer) }
            .to_string_lossy()
            .into_owned()
    }

    pub fn abi_version() -> u32 {
        // SAFETY: no pointer or ownership contract is involved in this function.
        unsafe { raw::fin_voice_builder_dsp_abi_version() }
    }
}

impl Drop for Dsp {
    fn drop(&mut self) {
        // SAFETY: this instance uniquely owns the handle and Drop runs once.
        unsafe { raw::fin_voice_builder_dsp_destroy(self.handle.as_ptr()) };
    }
}

#[cfg(test)]
mod tests {
    use super::{Dsp, FEATURE_DIMENSION, FRAME_SAMPLES};

    #[test]
    fn native_abi_reports_a_supported_major_version() {
        assert_eq!(Dsp::abi_version() >> 16, 1);
    }

    #[test]
    fn silence_frame_produces_finite_canonical_features() {
        let mut dsp = Dsp::new().expect("native DSP context should initialize");
        let frame = [0.0_f32; FRAME_SAMPLES];
        let features = dsp
            .extract_f32(&frame)
            .expect("silence frame should extract successfully");

        assert_eq!(features.len(), FEATURE_DIMENSION);
        assert!(features.iter().all(|value| value.is_finite()));
        assert_eq!(features[26], 0.0, "silence should be unvoiced");
        assert_eq!(features[31], 0.0, "reserved feature must stay zero");
        assert!(dsp.workspace_bytes() > 0);
        assert!(!dsp.active_backend_name().is_empty());
    }

    #[test]
    fn pcm16_and_f32_silence_follow_the_same_feature_contract() {
        let mut f32_dsp = Dsp::new().expect("f32 native DSP context should initialize");
        let mut pcm16_dsp = Dsp::new().expect("PCM16 native DSP context should initialize");
        let f32_frame = [0.0_f32; FRAME_SAMPLES];
        let pcm16_frame = [0_i16; FRAME_SAMPLES];

        let f32_features = f32_dsp.extract_f32(&f32_frame).expect("f32 extraction");
        let pcm16_features = pcm16_dsp
            .extract_pcm16(&pcm16_frame)
            .expect("PCM16 extraction");

        for (index, (lhs, rhs)) in f32_features.iter().zip(pcm16_features.iter()).enumerate() {
            assert!(
                (lhs - rhs).abs() <= 1.0e-6,
                "silence feature {index} differs: f32={lhs}, pcm16={rhs}"
            );
        }
    }
}
