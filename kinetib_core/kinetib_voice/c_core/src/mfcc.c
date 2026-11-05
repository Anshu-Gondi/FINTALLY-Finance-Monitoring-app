#include "mfcc.h"
#include "macros/cache_alignment.h"

#include <math.h>
#include <string.h>

#define FIN_MFCC_PI_F 3.14159265358979323846f

/*
 * MFCC's plan tables, workspace, input spectrum and temporary coefficient
 * vector should remain far below the project's 100 KiB software budget.
 * This is a source-level guardrail for the MFCC sub-operation; it does not
 * predict actual cache residency or include the other acoustic DSP stages.
 */
#define FIN_MFCC_SOFTWARE_WORKING_SET_BUDGET_BYTES (100U * 1024U)

_Static_assert(_Alignof(FinMfccPlan) >= FIN_VOICE_CACHE_LINE_BYTES,
               "FinMfccPlan must retain the 64-byte cache alignment");
_Static_assert(_Alignof(FinMfccWorkspace) >= FIN_VOICE_CACHE_LINE_BYTES,
               "FinMfccWorkspace must retain the 64-byte cache alignment");
_Static_assert(
    sizeof(FinMfccPlan) + sizeof(FinMfccWorkspace) +
        (sizeof(float) * FIN_MFCC_FFT_BINS) +
        (sizeof(float) * FIN_MFCC_COUNT) <=
            FIN_MFCC_SOFTWARE_WORKING_SET_BUDGET_BYTES,
    "MFCC state and temporary buffers exceed the 100 KiB software budget"
);

static float fin_mfcc_hz_to_mel(float hz)
{
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

static float fin_mfcc_mel_to_hz(float mel)
{
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

static int fin_mfcc_is_finite(float value)
{
    return isfinite(value) ? 1 : 0;
}

static FinMfccStatus fin_mfcc_validate_power_inputs(
    const float power[FIN_MFCC_FFT_BINS],
    float epsilon
)
{
    size_t bin;

    if (power == NULL) {
        return FIN_MFCC_ERROR_INVALID_ARGUMENT;
    }

    if (!fin_mfcc_is_finite(epsilon) || epsilon <= 0.0f) {
        return FIN_MFCC_ERROR_INVALID_CONFIG;
    }

    for (bin = 0u; bin < FIN_MFCC_FFT_BINS; ++bin) {
        if (!fin_mfcc_is_finite(power[bin]) || power[bin] < 0.0f) {
            return FIN_MFCC_ERROR_NUMERIC_FAILURE;
        }
    }

    return FIN_MFCC_OK;
}

FinMfccStatus fin_mfcc_init(
    FinMfccPlan *plan,
    uint32_t sample_rate_hz
)
{
    size_t i;
    const float nyquist = (float)sample_rate_hz * 0.5f;
    float min_mel;
    float max_mel;

    if (plan == NULL) {
        return FIN_MFCC_ERROR_INVALID_ARGUMENT;
    }

    /* Invalidate stale state even when a reinitialization request is invalid. */
    memset(plan, 0, sizeof(*plan));

    if (sample_rate_hz == 0u || !fin_mfcc_is_finite(nyquist) || nyquist <= 0.0f) {
        return FIN_MFCC_ERROR_INVALID_CONFIG;
    }

    plan->sample_rate_hz = sample_rate_hz;
    min_mel = fin_mfcc_hz_to_mel(0.0f);
    max_mel = fin_mfcc_hz_to_mel(nyquist);

    /* Preserve the existing acoustic_dsp.c bin mapping exactly. */
    for (i = 0u; i < FIN_MEL_FILTER_COUNT + 2u; ++i) {
        const float fraction =
            (float)i / (float)(FIN_MEL_FILTER_COUNT + 1u);
        const float mel = min_mel + fraction * (max_mel - min_mel);
        const float hz = fin_mfcc_mel_to_hz(mel);
        const float mapped_bin =
            ((float)(FIN_MFCC_FFT_SIZE + 1u) * hz) /
            (float)sample_rate_hz;
        long bin;

        if (!fin_mfcc_is_finite(mapped_bin)) {
            memset(plan, 0, sizeof(*plan));
            return FIN_MFCC_ERROR_NUMERIC_FAILURE;
        }

        /* mapped_bin is bounded near [0, 257], so this conversion is safe. */
        bin = (long)floorf(mapped_bin);

        if (bin < 0L) {
            bin = 0L;
        }
        if (bin > (long)(FIN_MFCC_FFT_BINS - 1u)) {
            bin = (long)(FIN_MFCC_FFT_BINS - 1u);
        }

        plan->mel_bins[i] = (uint16_t)bin;
    }

    /* Orthonormal DCT-II matrix, matching the pre-refactor implementation. */
    for (i = 0u; i < FIN_MFCC_COUNT; ++i) {
        size_t k;
        const float scale = (i == 0u)
            ? sqrtf(1.0f / (float)FIN_MEL_FILTER_COUNT)
            : sqrtf(2.0f / (float)FIN_MEL_FILTER_COUNT);

        for (k = 0u; k < FIN_MEL_FILTER_COUNT; ++k) {
            const float angle =
                FIN_MFCC_PI_F * (float)i *
                (2.0f * (float)k + 1.0f) /
                (2.0f * (float)FIN_MEL_FILTER_COUNT);
            const float coefficient = scale * cosf(angle);

            if (!fin_mfcc_is_finite(coefficient)) {
                memset(plan, 0, sizeof(*plan));
                return FIN_MFCC_ERROR_NUMERIC_FAILURE;
            }

            plan->dct[i * FIN_MEL_FILTER_COUNT + k] = coefficient;
        }
    }

    plan->initialized = 1u;
    return FIN_MFCC_OK;
}

FinMfccStatus fin_mfcc_extract_log_mel_power(
    const FinMfccPlan *plan,
    FinMfccWorkspace *workspace,
    const float power[FIN_MFCC_FFT_BINS],
    float epsilon,
    float output[FIN_MEL_FILTER_COUNT]
)
{
    size_t m;
    FinMfccStatus status;
    /*
     * Compute into a private aligned scratch array first. Besides improving
     * alignment predictability, this preserves caller output if a later Mel
     * filter produces a numeric failure and makes partial output/workspace
     * aliasing harmless until the successful commit below.
     */
    FIN_VOICE_CACHE_ALIGN float log_mel_scratch[FIN_MEL_FILTER_COUNT];

    if (plan == NULL || workspace == NULL || power == NULL || output == NULL) {
        return FIN_MFCC_ERROR_INVALID_ARGUMENT;
    }
    if (plan->initialized != 1u) {
        return FIN_MFCC_ERROR_NOT_INITIALIZED;
    }

    status = fin_mfcc_validate_power_inputs(power, epsilon);
    if (status != FIN_MFCC_OK) {
        return status;
    }

    /* Calculate into caller-owned scratch first; do not partially modify output. */
    for (m = 0u; m < FIN_MEL_FILTER_COUNT; ++m) {
        const size_t left = plan->mel_bins[m];
        const size_t center = plan->mel_bins[m + 1u];
        const size_t right = plan->mel_bins[m + 2u];
        float energy = 0.0f;
        size_t k;

        /* A zero-width triangle is deliberately treated as a zero-energy filter. */
        if (center > left) {
            for (k = left; k < center && k < FIN_MFCC_FFT_BINS; ++k) {
                const float weight =
                    (float)(k - left) / (float)(center - left);
                energy += power[k] * weight;
            }
        }

        if (right > center) {
            for (k = center; k <= right && k < FIN_MFCC_FFT_BINS; ++k) {
                const float weight =
                    (float)(right - k) / (float)(right - center);
                energy += power[k] * weight;
            }
        }

        if (!fin_mfcc_is_finite(energy)) {
            return FIN_MFCC_ERROR_NUMERIC_FAILURE;
        }

        log_mel_scratch[m] = logf(fmaxf(energy, epsilon));
        if (!fin_mfcc_is_finite(log_mel_scratch[m])) {
            return FIN_MFCC_ERROR_NUMERIC_FAILURE;
        }
    }

    /*
     * Commit only after all filters pass validation. memmove is deliberate:
     * callers may legally pass an output region overlapping workspace scratch.
     * The temporary array prevents partial results from escaping on failure.
     */
    memcpy(workspace->log_mel, log_mel_scratch, sizeof(log_mel_scratch));
    memmove(output, log_mel_scratch, sizeof(log_mel_scratch));
    return FIN_MFCC_OK;
}

FinMfccStatus fin_mfcc_extract_power(
    const FinMfccPlan *plan,
    FinMfccWorkspace *workspace,
    const float power[FIN_MFCC_FFT_BINS],
    float epsilon,
    float output[FIN_MFCC_COUNT]
)
{
    FinMfccStatus status;
    /*
     * The coefficient result is small, but it is kept aligned as a private
     * scratch array so the DCT can be vectorized later without changing the
     * public API. Results are copied to the caller only after all are finite.
     */
    FIN_VOICE_CACHE_ALIGN float coefficients[FIN_MFCC_COUNT];
    size_t c;

    if (plan == NULL || workspace == NULL || power == NULL || output == NULL) {
        return FIN_MFCC_ERROR_INVALID_ARGUMENT;
    }
    if (plan->initialized != 1u) {
        return FIN_MFCC_ERROR_NOT_INITIALIZED;
    }

    status = fin_mfcc_extract_log_mel_power(
        plan,
        workspace,
        power,
        epsilon,
        workspace->log_mel
    );
    if (status != FIN_MFCC_OK) {
        return status;
    }

    for (c = 0u; c < FIN_MFCC_COUNT; ++c) {
        float coefficient = 0.0f;
        size_t k;

        for (k = 0u; k < FIN_MEL_FILTER_COUNT; ++k) {
            coefficient += workspace->log_mel[k] *
                plan->dct[c * FIN_MEL_FILTER_COUNT + k];
        }

        if (!fin_mfcc_is_finite(coefficient)) {
            return FIN_MFCC_ERROR_NUMERIC_FAILURE;
        }
        coefficients[c] = coefficient;
    }

    /* Commit results only after all coefficients have passed validation. */
    memcpy(output, coefficients, sizeof(coefficients));
    return FIN_MFCC_OK;
}
