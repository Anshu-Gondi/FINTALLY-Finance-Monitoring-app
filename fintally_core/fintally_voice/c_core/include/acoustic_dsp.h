#ifndef FINTALLY_VOICE_ACOUSTIC_DSP_H
#define FINTALLY_VOICE_ACOUSTIC_DSP_H

/*
 * ============================================================================
 * FINTALLY VOICE — ACOUSTIC DSP PUBLIC ABI
 * ============================================================================
 *
 * This header defines the canonical acoustic feature-extraction interface
 * shared by:
 *
 *     1. offline_builder
 *     2. fintally_voice runtime
 *
 * Architectural contract:
 *
 *     PCM16 audio
 *         |
 *         v
 *     acoustic DSP
 *         |
 *         v
 *     FP32 feature vector
 *         |
 *         v
 *     VQ / codebook / recognition
 *
 *
 * NUMERICAL CONTRACT
 * ------------------
 *
 * Continuous acoustic features are always represented as float32.
 *
 * Discrete runtime values such as VQ indices, states and IDs use integer
 * representations separately.
 *
 *
 * RUNTIME CONTRACT
 * ---------------
 *
 * The hot path must:
 *
 *     - perform zero heap allocation
 *     - avoid filesystem/network access
 *     - avoid logging
 *     - operate on fixed-size buffers
 *     - use a preallocated workspace
 *     - avoid architecture checks inside frame processing
 *
 *
 * SIMD CONTRACT
 * -------------
 *
 * Scalar C is the mathematical reference implementation.
 *
 * The runtime dispatcher selects a supported backend once during initialization.
 * Architecture-specific vector types never cross the public C/Rust ABI.
 *
 * Important representation boundary:
 *   - acoustic feature extraction remains FP32 and preserves the canonical
 *     model-input semantics;
 *   - the current SIMD operation table accelerates explicitly quantized UINT8
 *     dot products/distances for codebook or candidate-scoring operations;
 *   - FP32 audio features are never silently quantized to enter a UINT8 kernel.
 *
 * This distinction lets the SIMD layer be available from the acoustic context
 * without changing the trained FP32 feature contract.
 *
 * ============================================================================
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Shared C23 alignment convention and the single canonical SIMD backend enum. */
#include "kernel/simd_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif


/* ============================================================================
 * VERSIONING
 * ========================================================================== */

/*
 * Increment MAJOR for an incompatible public layout/contract change. Increment
 * MINOR for backward-compatible interface additions such as explicit quantized
 * kernel facades and additional backend identifiers.
 *
 * Training artifacts can record this value so a codebook trained against one
 * feature contract is never accidentally used with another feature extractor.
 * The existing public struct layouts remain naturally aligned for compatibility
 * with Rust #[repr(C)] mirrors. 64-byte alignment is applied to private workspace
 * arrays, where it cannot change the public FFI struct layout.
 */
#define FIN_VOICE_DSP_ABI_MAJOR 1u
#define FIN_VOICE_DSP_ABI_MINOR 1u


/* ============================================================================
 * CANONICAL AUDIO FORMAT
 * ========================================================================== */

/*
 * The offline preparation layer produces:
 *
 *     16,000 Hz
 *     mono
 *     signed 16-bit PCM
 *
 * The DSP converts PCM16 samples to float32 internally before numerical
 * feature processing.
 */
#define FIN_VOICE_SAMPLE_RATE_HZ 16000u
#define FIN_VOICE_CHANNELS       1u
#define FIN_VOICE_PCM_BITS       16u


/* ============================================================================
 * FRAME CONFIGURATION
 * ========================================================================== */

/*
 * Canonical real-time frame:
 *
 *     20 ms = 320 samples at 16 kHz
 *
 * Canonical hop:
 *
 *     10 ms = 160 samples at 16 kHz
 *
 * These values should remain identical between the offline builder and the
 * embedded runtime unless the model/codebook ABI is deliberately regenerated.
 */
#define FIN_VOICE_FRAME_SAMPLES 320u
#define FIN_VOICE_HOP_SAMPLES   160u

#define FIN_VOICE_FRAME_MS 20u
#define FIN_VOICE_HOP_MS   10u


/* ============================================================================
 * FEATURE VECTOR
 * ========================================================================== */

/*
 * Fixed feature dimensionality.
 *
 * The model/codebook boundary is always:
 *
 *     float feature[32]
 *
 * Keeping the dimension compile-time constant allows:
 *
 *     - predictable memory usage
 *     - compiler vectorization
 *     - fixed-size FFI
 *     - fixed-size codebook layout
 *     - easier SIMD kernels
 */
#define FIN_VOICE_FEATURE_DIM 32u


/*
 * Canonical feature layout.
 *
 *  0..12   : MFCC 0..12
 * 13..22   : LPC coefficients 0..9
 * 23       : F1
 * 24       : F2
 * 25       : F3
 * 26       : fundamental frequency / F0
 * 27       : log energy
 * 28       : zero-crossing rate
 * 29       : spectral energy
 * 30       : spectral centroid
 * 31       : reserved / extension feature
 *
 * IMPORTANT:
 *
 * Once the codebook is trained, changing this order changes the model input
 * semantics and therefore requires retraining.
 */
typedef enum FinVoiceFeatureIndex {
    FIN_FEATURE_MFCC_0 = 0,
    FIN_FEATURE_MFCC_1 = 1,
    FIN_FEATURE_MFCC_2 = 2,
    FIN_FEATURE_MFCC_3 = 3,
    FIN_FEATURE_MFCC_4 = 4,
    FIN_FEATURE_MFCC_5 = 5,
    FIN_FEATURE_MFCC_6 = 6,
    FIN_FEATURE_MFCC_7 = 7,
    FIN_FEATURE_MFCC_8 = 8,
    FIN_FEATURE_MFCC_9 = 9,
    FIN_FEATURE_MFCC_10 = 10,
    FIN_FEATURE_MFCC_11 = 11,
    FIN_FEATURE_MFCC_12 = 12,

    FIN_FEATURE_LPC_0 = 13,
    FIN_FEATURE_LPC_1 = 14,
    FIN_FEATURE_LPC_2 = 15,
    FIN_FEATURE_LPC_3 = 16,
    FIN_FEATURE_LPC_4 = 17,
    FIN_FEATURE_LPC_5 = 18,
    FIN_FEATURE_LPC_6 = 19,
    FIN_FEATURE_LPC_7 = 20,
    FIN_FEATURE_LPC_8 = 21,
    FIN_FEATURE_LPC_9 = 22,

    FIN_FEATURE_FORMANT_F1 = 23,
    FIN_FEATURE_FORMANT_F2 = 24,
    FIN_FEATURE_FORMANT_F3 = 25,

    FIN_FEATURE_F0 = 26,
    FIN_FEATURE_LOG_ENERGY = 27,
    FIN_FEATURE_ZCR = 28,
    FIN_FEATURE_SPECTRAL_ENERGY = 29,
    FIN_FEATURE_SPECTRAL_CENTROID = 30,

    FIN_FEATURE_RESERVED_0 = 31
} FinVoiceFeatureIndex;


/* ============================================================================
 * STATUS CODES
 * ========================================================================== */

typedef enum FinVoiceDspStatus {
    FIN_DSP_OK = 0,

    FIN_DSP_ERROR_INVALID_ARGUMENT = 1,
    FIN_DSP_ERROR_INVALID_CONFIG = 2,
    FIN_DSP_ERROR_BUFFER_TOO_SMALL = 3,
    FIN_DSP_ERROR_INVALID_SAMPLE_RATE = 4,
    FIN_DSP_ERROR_INVALID_CHANNEL_COUNT = 5,
    FIN_DSP_ERROR_NOT_INITIALIZED = 6,
    FIN_DSP_ERROR_NUMERIC_FAILURE = 7,
    FIN_DSP_ERROR_UNSUPPORTED_BACKEND = 8,
    FIN_DSP_ERROR_WORKSPACE_TOO_SMALL = 9
} FinVoiceDspStatus;


/* ============================================================================
 * SIMD BACKEND
 * ========================================================================== */

/*
 * FinVoiceSimdBackend is defined by simd_dispatch.h so the acoustic ABI and
 * the UINT8 kernel dispatcher cannot drift into separate, incompatible enums.
 * The original FIN_SIMD_* numeric values are preserved; new backend values are
 * appended by the dispatcher header.
 */


/* ============================================================================
 * DSP CONFIGURATION
 * ========================================================================== */

typedef struct FinVoiceDspConfig {
    /*
     * Input sample rate.
     *
     * Runtime canonical value:
     *
     *     16000
     */
    uint32_t sample_rate_hz;

    /*
     * Number of input channels.
     *
     * Runtime canonical value:
     *
     *     1
     */
    uint16_t channels;

    /*
     * Number of samples processed in one frame.
     *
     * Canonical:
     *
     *     320
     */
    uint16_t frame_samples;

    /*
     * Hop between consecutive frames.
     *
     * Canonical:
     *
     *     160
     */
    uint16_t hop_samples;

    /*
     * Pre-emphasis coefficient.
     *
     * A value around 0.97 is a common starting point, but the actual
     * production value must be fixed during training and runtime.
     */
    float pre_emphasis;

    /*
     * Small value preventing log(0), division by zero and related numerical
     * instability.
     */
    float epsilon;

    /*
     * Requested SIMD backend.
     *
     * FIN_SIMD_AUTO is recommended for normal builds.
     */
    FinVoiceSimdBackend simd_backend;

} FinVoiceDspConfig;


/* ============================================================================
 * FEATURE VECTOR
 * ========================================================================== */

/*
 * Fixed-size FP32 feature vector.
 *
 * No heap allocation.
 */
typedef struct FinVoiceFeatureVector {
    /* Keep natural C/Rust FFI alignment; private workspace arrays are aligned. */
    float values[FIN_VOICE_FEATURE_DIM];
} FinVoiceFeatureVector;


/* ============================================================================
 * NORMALIZATION PARAMETERS
 * ========================================================================== */

/*
 * Feature normalization:
 *
 *     normalized[d] =
 *         (feature[d] - mean[d]) * inv_std[d]
 *
 * Storing inverse standard deviation avoids a division in the runtime hot
 * path.
 */
typedef struct FinVoiceFeatureNormalization {
    /* Public FFI layout remains naturally aligned; avoid an ABI alignment change. */
    float mean[FIN_VOICE_FEATURE_DIM];

    /*
     * 1 / standard_deviation
     */
    float inv_std[FIN_VOICE_FEATURE_DIM];
} FinVoiceFeatureNormalization;


/* ============================================================================
 * FEATURE EXTRACTION WORKSPACE
 * ========================================================================== */

/*
 * Workspace-backed DSP design.
 *
 * The exact internal layout remains private to acoustic_dsp.c. Hot arrays in
 * that private state are 64-byte aligned, but this descriptor is not over-aligned
 * so its C/Rust FFI layout stays stable.
 *
 * The caller obtains the required size once and provides a preallocated buffer.
 * The implementation reserves alignment padding and aligns the private state
 * inside this buffer. The workspace currently has a nominal 100 KiB software
 * target; alignment and size limits do not guarantee physical cache residency.
 * Frame processing performs zero malloc/free operations.
 */
typedef struct FinVoiceDspWorkspace {
    void *memory;

    size_t memory_size;
} FinVoiceDspWorkspace;


/* ============================================================================
 * DSP CONTEXT
 * ========================================================================== */

/*
 * DSP context.
 *
 * This object must be initialized once before audio processing.
 *
 * Runtime code should allocate it statically or during initialization.
 */
typedef struct FinVoiceDspContext {
    FinVoiceDspConfig config;

    FinVoiceSimdBackend active_backend;

    FinVoiceDspWorkspace workspace;

    bool initialized;
} FinVoiceDspContext;


/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

/*
 * Initialize a DSP configuration with Fintally Voice canonical defaults.
 *
 * Equivalent conceptual defaults:
 *
 *     sample_rate_hz = 16000
 *     channels       = 1
 *     frame_samples  = 320
 *     hop_samples    = 160
 *     pre_emphasis   = project default
 *     epsilon        = project default
 *     simd_backend   = AUTO
 */
void fin_voice_dsp_default_config(
    FinVoiceDspConfig *config
);


/*
 * Validate configuration without allocating memory.
 */
FinVoiceDspStatus fin_voice_dsp_validate_config(
    const FinVoiceDspConfig *config
);


/*
 * Return the number of bytes required for one initialized DSP workspace.
 *
 * This lets the caller allocate exactly once:
 *
 *     uint8_t workspace[required_size];
 *
 * or allocate it during startup.
 */
size_t fin_voice_dsp_workspace_size(
    const FinVoiceDspConfig *config
);


/*
 * Initialize a DSP context using caller-owned memory.
 *
 * No heap allocation is permitted inside this function.
 */
FinVoiceDspStatus fin_voice_dsp_init(
    FinVoiceDspContext *context,
    const FinVoiceDspConfig *config,
    void *workspace_memory,
    size_t workspace_size
);


/*
 * Reset streaming DSP state.
 *
 * This does not release memory.
 */
FinVoiceDspStatus fin_voice_dsp_reset(
    FinVoiceDspContext *context
);


/* ============================================================================
 * SIMD INFORMATION
 * ========================================================================== */

/*
 * Return the backend selected for this context's quantized UINT8 kernel calls.
 * The canonical FP32 acoustic feature pipeline remains scalar until a separate
 * FP32 operation family is added to the dispatcher.
 */
FinVoiceSimdBackend fin_voice_dsp_active_backend(
    const FinVoiceDspContext *context
);


/*
 * Convert a backend identifier to a human-readable constant string.
 *
 * The returned string is static storage and must not be freed.
 */
const char *fin_voice_dsp_backend_name(
    FinVoiceSimdBackend backend
);


/* ============================================================================
 * QUANTIZED CODEBOOK OPERATIONS
 * ========================================================================== */

/*
 * These operations use the backend selected by fin_voice_dsp_init(). Inputs
 * are already quantized UINT8 data; this API does not quantize FP32 audio
 * features. The scale/zero-point convention must match the quantizer used to
 * produce the input buffers and codebook.
 *
 * Returns FIN_DSP_OK on success and an appropriate FIN_DSP_ERROR_* status on
 * invalid arguments/configuration or rejected numeric input. Calls allocate no
 * memory and perform no CPU feature detection after initialization.
 */
FinVoiceDspStatus fin_voice_dsp_quantized_dot_u8(
    FinVoiceDspContext *context,
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    uint8_t zero_point,
    float *out_f32
);

FinVoiceDspStatus fin_voice_dsp_quantized_squared_distance_u8(
    FinVoiceDspContext *context,
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    float *out_squared_distance_f32
);

FinVoiceDspStatus fin_voice_dsp_quantized_nearest_u8(
    FinVoiceDspContext *context,
    const uint8_t *query,
    const uint8_t *centroids,
    size_t centroid_count,
    size_t dimension,
    float scale,
    size_t *out_index,
    float *out_squared_distance_f32
);


/* ============================================================================
 * FRAME PROCESSING
 * ========================================================================== */

/*
 * Extract one canonical FP32 feature vector from one audio frame.
 *
 * Input:
 *
 *     PCM16 mono samples
 *
 * Length:
 *
 *     frame_samples
 *
 * Output:
 *
 *     32 FP32 features
 *
 * No heap allocation.
 *
 * No internal ownership transfer.
 *
 * The output feature vector is fully overwritten.
 */
FinVoiceDspStatus fin_voice_dsp_extract_features(
    FinVoiceDspContext *context,
    const int16_t *pcm16,
    size_t sample_count,
    FinVoiceFeatureVector *output
);


/*
 * Extract features from an FP32 frame.
 *
 * This API is useful for the offline builder when the audio pipeline already
 * has FP32 samples after decoding/resampling.
 *
 * The numerical feature extraction path remains identical to the runtime
 * feature extractor.
 */
FinVoiceDspStatus fin_voice_dsp_extract_features_f32(
    FinVoiceDspContext *context,
    const float *samples,
    size_t sample_count,
    FinVoiceFeatureVector *output
);


/* ============================================================================
 * NORMALIZATION
 * ========================================================================== */

/*
 * Normalize one feature vector using precomputed FP32 statistics.
 *
 * This function performs:
 *
 *     (x - mean) * inv_std
 *
 * It is safe to use in the runtime hot path.
 */
FinVoiceDspStatus fin_voice_dsp_normalize_features(
    const FinVoiceFeatureVector *input,
    const FinVoiceFeatureNormalization *normalization,
    FinVoiceFeatureVector *output
);


/*
 * Combined operation:
 *
 *     extract
 *        ↓
 *     normalize
 *
 * This reduces temporary-buffer management for the runtime.
 */
FinVoiceDspStatus fin_voice_dsp_extract_normalized(
    FinVoiceDspContext *context,
    const int16_t *pcm16,
    size_t sample_count,
    const FinVoiceFeatureNormalization *normalization,
    FinVoiceFeatureVector *output
);


/*
 * FP32 input variant for offline training.
 */
FinVoiceDspStatus fin_voice_dsp_extract_normalized_f32(
    FinVoiceDspContext *context,
    const float *samples,
    size_t sample_count,
    const FinVoiceFeatureNormalization *normalization,
    FinVoiceFeatureVector *output
);


/* ============================================================================
 * STREAMING FRAME API
 * ========================================================================== */

/*
 * Push a PCM16 sample block into the streaming feature extractor.
 *
 * The function processes zero or more complete frames.
 *
 * `frames_produced` receives the number of feature vectors generated.
 *
 * The caller supplies the output array.
 *
 * No heap allocation.
 */
FinVoiceDspStatus fin_voice_dsp_push_pcm16(
    FinVoiceDspContext *context,
    const int16_t *samples,
    size_t sample_count,
    FinVoiceFeatureVector *outputs,
    size_t output_capacity,
    size_t *frames_produced
);


/*
 * FP32 streaming equivalent.
 *
 * Intended primarily for the offline builder.
 */
FinVoiceDspStatus fin_voice_dsp_push_f32(
    FinVoiceDspContext *context,
    const float *samples,
    size_t sample_count,
    FinVoiceFeatureVector *outputs,
    size_t output_capacity,
    size_t *frames_produced
);


/*
 * Flush any remaining samples in the streaming buffer.
 *
 * The implementation may zero-pad the final partial frame according to the
 * project training policy.
 */
FinVoiceDspStatus fin_voice_dsp_flush(
    FinVoiceDspContext *context,
    FinVoiceFeatureVector *outputs,
    size_t output_capacity,
    size_t *frames_produced
);


/* ============================================================================
 * INDIVIDUAL FEATURE KERNELS
 * ========================================================================== */

/*
 * These functions expose the individual feature families for offline
 * verification and unit testing.
 *
 * Runtime code should normally call:
 *
 *     fin_voice_dsp_extract_features()
 *
 * rather than invoking these individually.
 */


/* --------------------------------------------------------------------------
 * MFCC
 * -------------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_mfcc(
    const float *frame,
    size_t frame_samples,
    uint32_t sample_rate_hz,
    float *mfcc_out,
    size_t mfcc_count
);


/* --------------------------------------------------------------------------
 * LPC
 * -------------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_lpc(
    const float *frame,
    size_t frame_samples,
    float *lpc_out,
    size_t lpc_count
);


/* --------------------------------------------------------------------------
 * Formants
 * -------------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_formants(
    const float *frame,
    size_t frame_samples,
    uint32_t sample_rate_hz,
    float *formant_out,
    size_t formant_count
);


/* --------------------------------------------------------------------------
 * Pitch
 * -------------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_pitch_f0(
    const float *frame,
    size_t frame_samples,
    uint32_t sample_rate_hz,
    float *f0_hz
);


/* --------------------------------------------------------------------------
 * Energy / ZCR
 * -------------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_energy_zcr(
    const float *frame,
    size_t frame_samples,
    float *log_energy,
    float *zcr
);


/* ============================================================================
 * VALIDATION
 * ========================================================================== */

/*
 * Validate a feature vector.
 *
 * Returns FIN_DSP_OK only if every feature is finite.
 */
FinVoiceDspStatus fin_voice_dsp_validate_features(
    const FinVoiceFeatureVector *features
);


/*
 * Compare two feature vectors.
 *
 * Primarily intended for:
 *
 *     scalar vs SSE
 *     scalar vs AVX2
 *     scalar vs NEON
 *
 * Returns the maximum absolute difference.
 */
FinVoiceDspStatus fin_voice_dsp_max_abs_error(
    const FinVoiceFeatureVector *reference,
    const FinVoiceFeatureVector *candidate,
    float *max_error
);


/*
 * Return the compile-time ABI version.
 */
uint32_t fin_voice_dsp_abi_version(void);


/* ============================================================================
 * BUILD / PLATFORM INFORMATION
 * ========================================================================== */

/*
 * Returns nonzero only if the requested backend is linked, its complete
 * operations table is present, and its runtime support check succeeds. AUTO
 * is available because the mandatory scalar fallback always exists.
 */
int fin_voice_dsp_backend_available(
    FinVoiceSimdBackend backend
);


/*
 * Optional diagnostic function.
 *
 * Does not perform logging itself.
 *
 * The caller can inspect:
 *
 *     - ABI version
 *     - active SIMD backend
 *     - frame size
 *     - feature dimension
 */
typedef struct FinVoiceDspInfo {
    uint32_t abi_major;
    uint32_t abi_minor;

    uint32_t sample_rate_hz;
    uint32_t frame_samples;
    uint32_t hop_samples;
    uint32_t feature_dimension;

    FinVoiceSimdBackend active_backend;
} FinVoiceDspInfo;


FinVoiceDspStatus fin_voice_dsp_get_info(
    const FinVoiceDspContext *context,
    FinVoiceDspInfo *info
);


/* ============================================================================
 * C++ COMPATIBILITY
 * ========================================================================== */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FINTALLY_VOICE_ACOUSTIC_DSP_H */
