#include "lpc_formants.h"
#include "macros/cache_alignment.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define FIN_LPC_FORMANTS_PI_F 3.14159265358979323846f
#define FIN_LPC_FORMANTS_TWO_PI_F 6.28318530717958647692f
#define FIN_LPC_FORMANTS_EPSILON 1.0e-12f
#define FIN_LPC_REFLECTION_LIMIT 0.999f

/*
 * Why this bound exists:
 * Formant analysis owns a 257-float envelope and a few short ranking arrays.
 * Keep their declared footprint far below the 100 KiB tuning target so future
 * edits cannot quietly turn this frame-level kernel into a large scratch user.
 * Alignment padding is included conservatively (one cache line per array).
 */
#define FIN_LPC_FORMANTS_ESTIMATE_SCRATCH_BYTES \
    (sizeof(float) * (FIN_LPC_FORMANT_SPECTRUM_BINS + \
                      3u * FIN_FORMANT_COUNT) + \
     4u * FIN_VOICE_CACHE_LINE_BYTES)

_Static_assert(
    FIN_LPC_FORMANTS_ESTIMATE_SCRATCH_BYTES <=
        FIN_LPC_FORMANTS_WORKING_SET_BUDGET_BYTES,
    "LPC/formant scratch exceeds the configured software working-set budget"
);

static int fin_lpc_formants_is_finite(float value)
{
    return isfinite(value) ? 1 : 0;
}

static float fin_lpc_formants_clamp(
    float value,
    float minimum,
    float maximum
)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

FinLpcFormantsStatus fin_lpc_formants_autocorrelation(
    const float *windowed_frame,
    size_t frame_samples,
    float autocorrelation[FIN_LPC_ORDER + 1u]
)
{
    size_t i;
    size_t lag;
    FIN_VOICE_CACHE_ALIGN float result[FIN_LPC_ORDER + 1u];

    if (windowed_frame == NULL || autocorrelation == NULL) {
        return FIN_LPC_FORMANTS_ERROR_INVALID_ARGUMENT;
    }

    if (frame_samples <= FIN_LPC_ORDER) {
        return FIN_LPC_FORMANTS_ERROR_BUFFER_TOO_SMALL;
    }

    /*
     * Validate the complete frame before calculating anything. In addition to
     * clearer error handling, this prevents NaN/Inf from contaminating all lags.
     */
    for (i = 0u; i < frame_samples; ++i) {
        if (!fin_lpc_formants_is_finite(windowed_frame[i])) {
            return FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE;
        }
    }

    /*
     * Compute into private cache-line-aligned scratch and commit once complete.
     * Using i < frame_samples - lag avoids possible i + lag size_t overflow.
     */
    for (lag = 0u; lag <= FIN_LPC_ORDER; ++lag) {
        float sum = 0.0f;
        const size_t limit = frame_samples - lag;

        for (i = 0u; i < limit; ++i) {
            sum += windowed_frame[i] * windowed_frame[i + lag];
        }

        if (!fin_lpc_formants_is_finite(sum)) {
            return FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE;
        }
        result[lag] = sum;
    }

    memcpy(autocorrelation, result, sizeof(result));
    return FIN_LPC_FORMANTS_OK;
}

FinLpcFormantsStatus fin_lpc_formants_compute_lpc(
    const float autocorrelation[FIN_LPC_ORDER + 1u],
    float epsilon,
    float lpc_coefficients[FIN_LPC_ORDER]
)
{
    FIN_VOICE_CACHE_ALIGN float coefficients[FIN_LPC_ORDER + 1u] = {0.0f};
    FIN_VOICE_CACHE_ALIGN float previous[FIN_LPC_ORDER + 1u] = {0.0f};
    size_t i;
    size_t order;

    if (autocorrelation == NULL || lpc_coefficients == NULL) {
        return FIN_LPC_FORMANTS_ERROR_INVALID_ARGUMENT;
    }

    if (!fin_lpc_formants_is_finite(epsilon) || epsilon <= 0.0f) {
        return FIN_LPC_FORMANTS_ERROR_INVALID_CONFIG;
    }

    for (i = 0u; i <= FIN_LPC_ORDER; ++i) {
        if (!fin_lpc_formants_is_finite(autocorrelation[i])) {
            return FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE;
        }
    }

    /* Silence or near-silence has no useful all-pole model. */
    if (autocorrelation[0] <= epsilon) {
        const FIN_VOICE_CACHE_ALIGN float zero_coefficients[FIN_LPC_ORDER] =
            {0.0f};
        memcpy(lpc_coefficients, zero_coefficients, sizeof(zero_coefficients));
        return FIN_LPC_FORMANTS_OK;
    }

    coefficients[0] = 1.0f;

    {
        float prediction_error = autocorrelation[0];

        for (order = 1u; order <= FIN_LPC_ORDER; ++order) {
            float reflection;
            float sum = autocorrelation[order];

            memcpy(previous, coefficients, sizeof(coefficients));

            for (i = 1u; i < order; ++i) {
                sum += previous[i] * autocorrelation[order - i];
            }

            if (!fin_lpc_formants_is_finite(sum) ||
                !fin_lpc_formants_is_finite(prediction_error)) {
                return FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE;
            }

            if (fabsf(prediction_error) <= epsilon) {
                break;
            }

            reflection = -sum / prediction_error;
            if (!fin_lpc_formants_is_finite(reflection)) {
                return FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE;
            }

            /*
             * Clamping the reflection coefficient below unit magnitude prevents
             * an unstable update from exploding the prediction-error recursion.
             */
            reflection = fin_lpc_formants_clamp(
                reflection,
                -FIN_LPC_REFLECTION_LIMIT,
                FIN_LPC_REFLECTION_LIMIT
            );

            coefficients[order] = reflection;

            for (i = 1u; i < order; ++i) {
                coefficients[i] = previous[i] +
                    reflection * previous[order - i];
            }

            prediction_error *= 1.0f - reflection * reflection;
            if (!fin_lpc_formants_is_finite(prediction_error)) {
                return FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE;
            }
            if (prediction_error < epsilon) {
                prediction_error = epsilon;
            }
        }
    }

    /* Validate every result before changing the caller's output buffer. */
    for (i = 0u; i < FIN_LPC_ORDER; ++i) {
        if (!fin_lpc_formants_is_finite(coefficients[i + 1u])) {
            return FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE;
        }
    }

    memcpy(lpc_coefficients, coefficients + 1u,
           FIN_LPC_ORDER * sizeof(*lpc_coefficients));
    return FIN_LPC_FORMANTS_OK;
}

FinLpcFormantsStatus fin_lpc_formants_estimate(
    const float lpc_coefficients[FIN_LPC_ORDER],
    uint32_t sample_rate_hz,
    float formants[FIN_FORMANT_COUNT]
)
{
    FIN_VOICE_CACHE_ALIGN float envelope[FIN_LPC_FORMANT_SPECTRUM_BINS];
    FIN_VOICE_CACHE_ALIGN float best_frequency[FIN_FORMANT_COUNT] = {0.0f};
    FIN_VOICE_CACHE_ALIGN float best_strength[FIN_FORMANT_COUNT] =
        {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    FIN_VOICE_CACHE_ALIGN float result_formants[FIN_FORMANT_COUNT] = {0.0f};
    size_t i;
    size_t bin;

    if (lpc_coefficients == NULL || formants == NULL) {
        return FIN_LPC_FORMANTS_ERROR_INVALID_ARGUMENT;
    }

    if (sample_rate_hz < 8000u) {
        return FIN_LPC_FORMANTS_ERROR_INVALID_CONFIG;
    }

    for (i = 0u; i < FIN_LPC_ORDER; ++i) {
        if (!fin_lpc_formants_is_finite(lpc_coefficients[i])) {
            return FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE;
        }
    }

    /*
     * Evaluate only the envelope bins that can affect a valid formant peak.
     *
     * The selection pass accepts only bins whose frequencies lie in
     * [FIN_FORMANT_MIN_HZ, FIN_FORMANT_MAX_HZ], and compares each accepted bin
     * with its immediate neighbours. Values farther outside that band cannot
     * affect any returned formant. A two-bin halo on each side is conservative
     * against float rounding in the frequency-to-bin calculation.
     *
     * Keep the original envelope formula and the original sinf/cosf calls for
     * every potentially observable bin. This deliberately prioritizes exact
     * formant-bin selection over a phase recurrence: testing showed that tiny
     * recurrence rounding errors can change the winning peak on nearly tied
     * envelopes, causing a discrete F1/F2/F3 shift despite tiny pointwise error.
     */
    {
        const float bins_per_hz =
            (float)FIN_LPC_FORMANT_FFT_SIZE / (float)sample_rate_hz;
        const float min_position = FIN_FORMANT_MIN_HZ * bins_per_hz;
        const float max_position = FIN_FORMANT_MAX_HZ * bins_per_hz;
        size_t first_envelope_bin = (size_t)floorf(min_position);
        size_t last_envelope_bin = (size_t)ceilf(max_position);

        first_envelope_bin = first_envelope_bin > 2u
            ? first_envelope_bin - 2u
            : 0u;
        if (last_envelope_bin < FIN_LPC_FORMANT_SPECTRUM_BINS - 3u) {
            last_envelope_bin += 2u;
        } else {
            last_envelope_bin = FIN_LPC_FORMANT_SPECTRUM_BINS - 1u;
        }

        for (bin = 0u; bin < FIN_LPC_FORMANT_SPECTRUM_BINS; ++bin) {
            /*
             * Explicitly initialize skipped slots. Selection should never read
             * them, but this makes the range invariant robust to future edits
             * and avoids uninitialized scratch values under memory diagnostics.
             */
            if (bin < first_envelope_bin || bin > last_envelope_bin) {
                envelope[bin] = 0.0f;
                continue;
            }

            const float frequency =
                (float)bin * (float)sample_rate_hz /
                (float)FIN_LPC_FORMANT_FFT_SIZE;
            const float omega = FIN_LPC_FORMANTS_TWO_PI_F *
                frequency / (float)sample_rate_hz;
            float denominator_real = 1.0f;
            float denominator_imag = 0.0f;
            float magnitude_squared;

            for (i = 0u; i < FIN_LPC_ORDER; ++i) {
                const float phase = -omega * (float)(i + 1u);
                denominator_real += lpc_coefficients[i] * cosf(phase);
                denominator_imag += lpc_coefficients[i] * sinf(phase);
            }

            magnitude_squared = denominator_real * denominator_real +
                denominator_imag * denominator_imag;

            if (!fin_lpc_formants_is_finite(magnitude_squared)) {
                return FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE;
            }

            envelope[bin] = 1.0f / fmaxf(
                magnitude_squared,
                FIN_LPC_FORMANTS_EPSILON
            );

            if (!fin_lpc_formants_is_finite(envelope[bin])) {
                return FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE;
            }
        }
    }

    /*
     * Retain strong, separated local maxima rather than arbitrary high bins.
     * The three slots are amplitude-ranked until the final frequency sort.
     */
    for (bin = 1u; bin + 1u < FIN_LPC_FORMANT_SPECTRUM_BINS; ++bin) {
        const float frequency =
            (float)bin * (float)sample_rate_hz /
            (float)FIN_LPC_FORMANT_FFT_SIZE;
        const float strength = envelope[bin];
        size_t slot;
        size_t weakest_slot = 0u;
        int near_existing = 0;

        if (frequency < FIN_FORMANT_MIN_HZ || frequency > FIN_FORMANT_MAX_HZ) {
            continue;
        }

        if (!(strength > envelope[bin - 1u] &&
              strength >= envelope[bin + 1u])) {
            continue;
        }

        for (slot = 0u; slot < FIN_FORMANT_COUNT; ++slot) {
            if (best_frequency[slot] > 0.0f &&
                fabsf(best_frequency[slot] - frequency) <
                    FIN_FORMANT_MIN_DISTANCE_HZ) {
                near_existing = 1;
                if (strength > best_strength[slot]) {
                    best_frequency[slot] = frequency;
                    best_strength[slot] = strength;
                }
                break;
            }
        }

        if (near_existing) {
            continue;
        }

        for (slot = 1u; slot < FIN_FORMANT_COUNT; ++slot) {
            if (best_strength[slot] < best_strength[weakest_slot]) {
                weakest_slot = slot;
            }
        }

        if (strength > best_strength[weakest_slot]) {
            best_frequency[weakest_slot] = frequency;
            best_strength[weakest_slot] = strength;
        }
    }

    /* Sort by frequency so slots map to F1/F2/F3 regardless of peak strength. */
    for (i = 0u; i < FIN_FORMANT_COUNT; ++i) {
        size_t j;
        for (j = i + 1u; j < FIN_FORMANT_COUNT; ++j) {
            const int move_j_first =
                best_frequency[i] == 0.0f ||
                (best_frequency[j] > 0.0f &&
                 best_frequency[j] < best_frequency[i]);

            if (move_j_first) {
                const float frequency_tmp = best_frequency[i];
                const float strength_tmp = best_strength[i];
                best_frequency[i] = best_frequency[j];
                best_strength[i] = best_strength[j];
                best_frequency[j] = frequency_tmp;
                best_strength[j] = strength_tmp;
            }
        }
    }

    result_formants[0] = best_frequency[0];
    result_formants[1] = best_frequency[1];
    result_formants[2] = best_frequency[2];

    /* Commit only after the complete envelope and selection pass succeeded. */
    memcpy(formants, result_formants, sizeof(result_formants));
    return FIN_LPC_FORMANTS_OK;
}
