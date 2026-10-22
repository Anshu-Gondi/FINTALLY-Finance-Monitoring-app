#ifndef FINTALLY_VOICE_ENERGY_ZCR_H
#define FINTALLY_VOICE_ENERGY_ZCR_H

/*
 * FinTally Voice — time-domain energy, zero-crossing rate, and spectrum stats.
 *
 * Language/ownership contract:
 *   - ISO C23; this C core intentionally exposes a C-only API.
 *   - All sample and power inputs are FP32 and remain caller-owned.
 *   - No heap allocations, hidden global state, or retained input pointers.
 *   - Output structures are written only after the full operation succeeds.
 *
 * Canonical feature mapping:
 *   log_energy          -> FIN_FEATURE_LOG_ENERGY        (27)
 *   zero_crossing_rate  -> FIN_FEATURE_ZCR              (28)
 *   spectral_energy     -> FIN_FEATURE_SPECTRAL_ENERGY  (29)
 *   spectral_centroid   -> FIN_FEATURE_SPECTRAL_CENTROID (30)
 *
 * Cache/alignment design note:
 *   Public result structures contain only two or four scalar outputs. They keep
 *   their natural layout to preserve the C/Rust FFI ABI; do not add alignas(64)
 *   to these public structures. The implementation aligns private result-staging
 *   scratch instead. The long-lived frame and power-spectrum buffers belong to
 *   the aligned acoustic DSP workspace. This API still accepts arbitrary input
 *   alignment, so standalone callers are not required to align their buffers.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#error "FinTally Voice c_core is C-only; compile this interface as C23."
#endif

#define FIN_ENERGY_ZCR_ABI_VERSION 1u

/*
 * A 100 KiB software working-set target guides the processing tile sizes in
 * energy_zcr.c. The selected tile lengths are multiples of a 64-byte cache line
 * in bytes, which keeps successive tile boundaries aligned when the input base
 * is aligned. It is not a claim about hardware cache capacity or residency.
 */
#define FIN_ENERGY_ZCR_WORKING_SET_BUDGET_BYTES (100u * 1024u)
#define FIN_ENERGY_ZCR_TIME_TILE_SAMPLES        256u
#define FIN_ENERGY_ZCR_SPECTRUM_TILE_BINS       64u

typedef enum FinEnergyZcrStatus {
    FIN_ENERGY_ZCR_OK = 0,
    FIN_ENERGY_ZCR_ERROR_INVALID_ARGUMENT = 1,
    FIN_ENERGY_ZCR_ERROR_INVALID_CONFIG = 2,
    FIN_ENERGY_ZCR_ERROR_BUFFER_TOO_SMALL = 3,
    FIN_ENERGY_ZCR_ERROR_NUMERIC_FAILURE = 4
} FinEnergyZcrStatus;

typedef struct FinEnergyZcrTimeFeatures {
    float log_energy;
    float zero_crossing_rate;
} FinEnergyZcrTimeFeatures;

typedef struct FinEnergyZcrSpectralFeatures {
    float spectral_energy;
    float spectral_centroid_hz;
} FinEnergyZcrSpectralFeatures;

typedef struct FinEnergyZcrFeatures {
    float log_energy;
    float zero_crossing_rate;
    float spectral_energy;
    float spectral_centroid_hz;
} FinEnergyZcrFeatures;

/*
 * Compute log mean-square energy and zero-crossing rate.
 *
 * `frame` must contain `frame_samples` finite FP32 samples. `epsilon` must be
 * finite and strictly positive and is the same log floor used during training.
 * For a one-sample frame, ZCR is defined as zero. A zero-crossing is counted
 * when the sign category changes using the project's existing convention:
 * negative samples are below zero; zero belongs to the nonnegative category.
 *
 * The implementation walks the input in fixed-size tiles while keeping one
 * accumulator and the previous sample across tile boundaries. The tile only
 * bounds the loop's active working chunk; it does not copy the input or create
 * a temporary frame. Output is committed only after successful validation.
 */
FinEnergyZcrStatus fin_energy_zcr_compute_time_domain(
    const float *frame,
    size_t frame_samples,
    float epsilon,
    FinEnergyZcrTimeFeatures *output
);

/*
 * Compute the canonical spectral statistics from a one-sided power spectrum:
 * average bin power, and power-weighted frequency centroid in Hz.
 *
 * `fft_size` is the original FFT length and `spectrum_bins` must equal
 * floor(fft_size / 2) + 1. Power values must be finite and non-negative.
 * Bins are visited in ascending order (including across tile boundaries) so
 * the FP32 accumulation order remains stable for the training/runtime contract.
 */
FinEnergyZcrStatus fin_energy_zcr_compute_spectral(
    const float *power_spectrum,
    size_t spectrum_bins,
    size_t fft_size,
    uint32_t sample_rate_hz,
    float epsilon,
    FinEnergyZcrSpectralFeatures *output
);

/*
 * Combined interface used by acoustic_dsp.c. It calculates all four features
 * from one source frame and its matching one-sided power spectrum. Results are
 * committed together, so a spectral error does not leave partial output from
 * the already-completed time-domain operation.
 */
FinEnergyZcrStatus fin_energy_zcr_compute(
    const float *frame,
    size_t frame_samples,
    const float *power_spectrum,
    size_t spectrum_bins,
    size_t fft_size,
    uint32_t sample_rate_hz,
    float epsilon,
    FinEnergyZcrFeatures *output
);

#endif /* FINTALLY_VOICE_ENERGY_ZCR_H */
