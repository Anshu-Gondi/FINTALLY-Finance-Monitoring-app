//! FinTally Voice — native FFI boundary.
//!
//! Generated declarations remain internal to this crate. Public consumers
//! receive validated metadata; safe wrappers for native pipeline operations
//! will be added as their ownership and buffer contracts are defined.

mod raw {
    #![allow(
        non_camel_case_types,
        non_snake_case,
        non_upper_case_globals,
        dead_code
    )]

    include!(concat!(env!("OUT_DIR"), "/fin_voice_bindings.rs"));
}

// Preserve existing internal paths such as `crate::ffi::FinVoicePipeline`,
// but do not expose generated raw declarations as a public API.
pub(crate) use raw::*;

use core::mem::{size_of, MaybeUninit};
use std::{error::Error, fmt};

/// Validated summary of the linked native library's ABI and audio contract.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NativeAbiInfo {
    pub dsp_abi_version: u32,
    pub vq_abi_version: u32,
    pub feature_dimension: u32,
    pub sample_rate_hz: u32,
    pub frame_samples: u32,
    pub hop_samples: u32,
}

/// Failure while validating the native ABI.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AbiCheckError {
    NativeProbeFailed(i32),
    StructSizeMismatch {
        expected: u32,
        actual: u32,
    },
    ValueMismatch {
        field: &'static str,
        expected: u32,
        actual: u32,
    },
}

impl fmt::Display for AbiCheckError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::NativeProbeFailed(status) => {
                write!(formatter, "native ABI probe failed with status {status}")
            }
            Self::StructSizeMismatch { expected, actual } => write!(
                formatter,
                "FinVoiceFfiAbiInfo size mismatch: Rust expects \
                {expected} bytes, native shim reports {actual}"
            ),
            Self::ValueMismatch {
                field,
                expected,
                actual,
            } => write!(
                formatter,
                "native ABI mismatch for {field}: \
                expected {expected}, received {actual}"
            ),
        }
    }
}

impl Error for AbiCheckError {}

/// Validate the ABI and audio contract during explicit initialization.
///
/// Call this outside the real-time audio path. This function makes one native
/// call and performs no application-level heap allocation.
pub fn verify_native_abi() -> Result<NativeAbiInfo, AbiCheckError> {
    let mut output = MaybeUninit::<raw::FinVoiceFfiAbiInfo>::zeroed();

    // SAFETY:
    // - `output.as_mut_ptr()` is non-null and correctly aligned.
    // - The pointer refers to writable storage for the complete C structure.
    // - The shim must initialize every field before returning success.
    let status = unsafe { raw::fin_voice_ffi_get_abi_info(output.as_mut_ptr()) };

    if status != 0 {
        return Err(AbiCheckError::NativeProbeFailed(status));
    }

    // SAFETY: the shim's success contract guarantees every field is written.
    let info = unsafe { output.assume_init() };

    let expected_size = u32::try_from(size_of::<raw::FinVoiceFfiAbiInfo>())
        .expect("FinVoiceFfiAbiInfo size must fit in u32");

    if info.struct_size != expected_size {
        return Err(AbiCheckError::StructSizeMismatch {
            expected: expected_size,
            actual: info.struct_size,
        });
    }

    let expected_dsp = (raw::FIN_VOICE_DSP_ABI_MAJOR << 16) | raw::FIN_VOICE_DSP_ABI_MINOR;

    let expected_vq = (raw::FIN_VQ_ABI_MAJOR << 16) | raw::FIN_VQ_ABI_MINOR;

    let checks = [
        ("DSP ABI version", expected_dsp, info.dsp_abi_version),
        ("VQ ABI version", expected_vq, info.vq_abi_version),
        (
            "feature dimension",
            raw::FIN_VOICE_FEATURE_DIM,
            info.feature_dimension,
        ),
        (
            "sample rate",
            raw::FIN_VOICE_SAMPLE_RATE_HZ,
            info.sample_rate_hz,
        ),
        (
            "frame size",
            raw::FIN_VOICE_FRAME_SAMPLES,
            info.frame_samples,
        ),
        ("hop size", raw::FIN_VOICE_HOP_SAMPLES, info.hop_samples),
    ];

    for (field, expected, actual) in checks {
        if actual != expected {
            return Err(AbiCheckError::ValueMismatch {
                field,
                expected,
                actual,
            });
        }
    }

    Ok(NativeAbiInfo {
        dsp_abi_version: info.dsp_abi_version,
        vq_abi_version: info.vq_abi_version,
        feature_dimension: info.feature_dimension,
        sample_rate_hz: info.sample_rate_hz,
        frame_samples: info.frame_samples,
        hop_samples: info.hop_samples,
    })
}
