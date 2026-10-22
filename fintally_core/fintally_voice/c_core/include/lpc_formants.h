#ifndef FINTALLY_VOICE_LPC_FORMANTS_H
#define FINTALLY_VOICE_LPC_FORMANTS_H

/*
 * Fintally Voice — LPC and formant analysis interface.
 *
 * ISO C23 module. This header contains declarations only; implementation is
 * in src/lpc_formants.c. Public caller-owned arrays keep their ordinary C
 * alignment requirements: forcing 64-byte alignment on every caller buffer
 * would add an unnecessary API constraint and would not guarantee L1 residency.
 *
 * Private, repeatedly accessed scratch arrays in lpc_formants.c use the shared
 * FIN_VOICE_CACHE_ALIGN macro. Keeping that choice private preserves function
 * signatures and avoids introducing a new public workspace ABI just to align
 * a small (about 1 KiB) temporary spectral envelope.
 *
 * The implementation's active scratch working set is asserted to fit inside
 * the 100 KiB software target. This is a working-set policy, not a guarantee
 * about physical cache occupancy or L2 misses.
 *
 * Canonical acoustic feature layout integration:
 *   output LPC coefficients[0..9] -> feature dimensions 13..22
 *   output formants[0..2]         -> dimensions 23..25 (F1, F2, F3)
 *
 * Formants are returned in ascending frequency order. Missing estimates are
 * zero. Continuous values remain FP32; no heap allocation is performed.
 */

#include <stddef.h>
#include <stdint.h>

#define FIN_LPC_ORDER 10u
#define FIN_FORMANT_COUNT 3u
#define FIN_LPC_FORMANT_FFT_SIZE 512u
#define FIN_LPC_FORMANT_SPECTRUM_BINS \
    (FIN_LPC_FORMANT_FFT_SIZE / 2u + 1u)

#define FIN_FORMANT_MIN_HZ 250.0f
#define FIN_FORMANT_MAX_HZ 4000.0f
#define FIN_FORMANT_MIN_DISTANCE_HZ 180.0f

/*
 * Keep the cache budget visible for diagnostics without exposing any private
 * scratch layout as part of the ABI. Tuning it does not change feature math.
 */
#define FIN_LPC_FORMANTS_WORKING_SET_BUDGET_BYTES (100u * 1024u)

typedef enum FinLpcFormantsStatus {
    FIN_LPC_FORMANTS_OK = 0,
    FIN_LPC_FORMANTS_ERROR_INVALID_ARGUMENT = 1,
    FIN_LPC_FORMANTS_ERROR_INVALID_CONFIG = 2,
    FIN_LPC_FORMANTS_ERROR_BUFFER_TOO_SMALL = 3,
    FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE = 4
} FinLpcFormantsStatus;

/*
 * Compute autocorrelation r[0..FIN_LPC_ORDER] for a windowed FP32 frame.
 * frame_samples must be greater than FIN_LPC_ORDER. The output array is
 * committed only when every lag has produced a finite result.
 */
FinLpcFormantsStatus fin_lpc_formants_autocorrelation(
    const float *windowed_frame,
    size_t frame_samples,
    float autocorrelation[FIN_LPC_ORDER + 1u]
);

/*
 * Levinson-Durbin recursion from autocorrelation values to 10 LPC terms.
 * epsilon is a positive finite numerical floor. Silence produces zero LPCs.
 * Output is committed only after all coefficients have been checked.
 */
FinLpcFormantsStatus fin_lpc_formants_compute_lpc(
    const float autocorrelation[FIN_LPC_ORDER + 1u],
    float epsilon,
    float lpc_coefficients[FIN_LPC_ORDER]
);

/*
 * Estimate the three strongest separated local maxima of the LPC spectral
 * envelope. Returned frequencies are sorted low-to-high for direct mapping to
 * F1, F2, and F3. Missing estimates are zero. Output is committed only after
 * the spectral envelope and all selected values have passed finite checks.
 * sample_rate_hz must be at least 8 kHz for the configured 4 kHz ceiling.
 */
FinLpcFormantsStatus fin_lpc_formants_estimate(
    const float lpc_coefficients[FIN_LPC_ORDER],
    uint32_t sample_rate_hz,
    float formants[FIN_FORMANT_COUNT]
);

#endif /* FINTALLY_VOICE_LPC_FORMANTS_H */
