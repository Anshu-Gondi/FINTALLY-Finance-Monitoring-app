#ifndef FINTALLY_VOICE_MFCC_H
#define FINTALLY_VOICE_MFCC_H

#include <stddef.h>
#include <stdint.h>

#include "macros/cache_alignment.h"

#if defined(__cplusplus)
#error "Fintally Voice c_core is C-only; compile this interface as C23."
#endif

/*
 * Fintally Voice — MFCC extraction module.
 *
 * This module preserves the current acoustic_dsp.c feature contract:
 *   - mono 16 kHz pipeline (sample rate is passed at plan initialization)
 *   - 320-sample frames are processed upstream
 *   - 512-point FFT and 257-bin one-sided power spectrum
 *   - 26 triangular Mel filters
 *   - 13 orthonormal DCT-II MFCC coefficients
 *
 * The caller owns the plan/workspace and reuses them. The extraction functions
 * perform no allocation. Initialize a plan before extraction; do not mutate a
 * plan or share one workspace between concurrent calls.
 *
 * This is not a Whisper model frontend. It emits this project's compact MFCC
 * representation and exposes log-Mel energies for diagnostics/future work.
 *
 * Cache / FFI layout note:
 *   The hot plan tables and log-Mel scratch are 64-byte aligned using the
 *   shared project macro. This raises the alignment requirement of these two
 *   public MFCC structs and changes their layout. Rebuild C dependants after
 *   upgrading this header. A Rust type that directly mirrors either struct
 *   must use #[repr(C, align(64))] and the exact field order. Prefer opaque
 *   pointers across FFI when the Rust side does not need to inspect fields.
 *
 * Alignment is a starting-address guarantee, not a promise that data stays in
 * L1 or avoids L2. The MFCC working set is already small; alignment is used
 * for predictable access and future vectorization, not as a substitute for
 * measuring cache misses.
 */
#define FIN_MFCC_COUNT             13u
#define FIN_MEL_FILTER_COUNT       26u
#define FIN_MFCC_FFT_SIZE          512u
#define FIN_MFCC_FFT_BINS          (FIN_MFCC_FFT_SIZE / 2u + 1u)

typedef enum FinMfccStatus {
    FIN_MFCC_OK = 0,
    FIN_MFCC_ERROR_INVALID_ARGUMENT = 1,
    FIN_MFCC_ERROR_INVALID_CONFIG = 2,
    FIN_MFCC_ERROR_NOT_INITIALIZED = 3,
    FIN_MFCC_ERROR_NUMERIC_FAILURE = 4
} FinMfccStatus;

typedef struct FinMfccPlan {
    uint32_t sample_rate_hz;

    /*
     * The DCT matrix is read for every frame. Give its first element a cache
     * line boundary so the matrix has a predictable base address. Rows are
     * still compact (26 floats) to avoid wasting space on padded row strides.
     */
    FIN_VOICE_CACHE_ALIGN float dct[
        FIN_MFCC_COUNT * FIN_MEL_FILTER_COUNT
    ];

    /*
     * Bin boundaries are also consumed once per filter during each frame.
     * Align the table start; keep the entries compact uint16_t values.
     */
    FIN_VOICE_CACHE_ALIGN uint16_t mel_bins[FIN_MEL_FILTER_COUNT + 2u];
    uint32_t initialized;
} FinMfccPlan;

typedef struct FinMfccWorkspace {
    /*
     * This scratch is written by the Mel filterbank and reread by the DCT.
     * Member alignment makes the struct itself 64-byte aligned. The workspace
     * must be caller-owned and 64-byte aligned when stored outside a normally
     * declared FinMfccWorkspace object (for example, via a custom Rust allocator).
     */
    FIN_VOICE_CACHE_ALIGN float log_mel[FIN_MEL_FILTER_COUNT];
} FinMfccWorkspace;

/*
 * Precompute Mel-bin boundaries and the orthonormal DCT-II matrix once.
 * `plan` must point to storage aligned for FinMfccPlan (64 bytes).
 */
[[nodiscard]] FinMfccStatus fin_mfcc_init(
    FinMfccPlan *plan,
    uint32_t sample_rate_hz
);

/*
 * Convert the caller's one-sided power spectrum into MFCC coefficients.
 * `power` must contain FIN_MFCC_FFT_BINS finite, non-negative FP32 values.
 * `epsilon` is the same positive log-floor used by the training pipeline.
 * No allocation occurs; plan and workspace are caller-owned/reused. `plan`
 * and `workspace` must satisfy their 64-byte type alignment requirements.
 *
 * On success, output contains all FIN_MFCC_COUNT coefficients. On failure,
 * output is left unchanged.
 */
[[nodiscard]] FinMfccStatus fin_mfcc_extract_power(
    const FinMfccPlan *plan,
    FinMfccWorkspace *workspace,
    const float power[FIN_MFCC_FFT_BINS],
    float epsilon,
    float output[FIN_MFCC_COUNT]
);

/* Log-Mel-only variant for diagnostics or a future dedicated ASR frontend. */
[[nodiscard]] FinMfccStatus fin_mfcc_extract_log_mel_power(
    const FinMfccPlan *plan,
    FinMfccWorkspace *workspace,
    const float power[FIN_MFCC_FFT_BINS],
    float epsilon,
    float output[FIN_MEL_FILTER_COUNT]
);

#endif /* FINTALLY_VOICE_MFCC_H */
