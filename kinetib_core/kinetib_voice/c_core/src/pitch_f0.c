#include "pitch_f0.h"

#include <float.h>
#include <math.h>

static int fin_pitch_f0_is_finite(float value)
{
    return isfinite(value) ? 1 : 0;
}

static float fin_pitch_f0_clamp(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

FinPitchF0Config fin_pitch_f0_default_config(void)
{
    const FinPitchF0Config config = {
        .min_hz = FIN_PITCH_F0_DEFAULT_MIN_HZ,
        .max_hz = FIN_PITCH_F0_DEFAULT_MAX_HZ,
        .yin_threshold = FIN_PITCH_F0_DEFAULT_YIN_THRESHOLD,
        .fallback_threshold = FIN_PITCH_F0_DEFAULT_FALLBACK_THRESHOLD,
        .min_rms = FIN_PITCH_F0_DEFAULT_MIN_RMS
    };

    return config;
}

static FinPitchF0Status fin_pitch_f0_validate(
    const float *frame,
    size_t frame_samples,
    uint32_t sample_rate_hz,
    const FinPitchF0Config *config,
    const FinPitchF0Workspace *workspace,
    const FinPitchF0Result *result
)
{
    size_t i;

    if (frame == NULL || config == NULL || workspace == NULL || result == NULL) {
        return FIN_PITCH_F0_ERROR_INVALID_ARGUMENT;
    }

    /* Check the fixed workspace limit before reading any caller samples. */
    if (frame_samples > FIN_PITCH_F0_MAX_FRAME_SAMPLES) {
        return FIN_PITCH_F0_ERROR_FRAME_TOO_LARGE;
    }
    if (frame_samples <= FIN_PITCH_F0_MIN_PAIRS) {
        return FIN_PITCH_F0_ERROR_INVALID_CONFIG;
    }
    if (sample_rate_hz == 0u) {
        return FIN_PITCH_F0_ERROR_INVALID_CONFIG;
    }

    if (!fin_pitch_f0_is_finite(config->min_hz) ||
        !fin_pitch_f0_is_finite(config->max_hz) ||
        !fin_pitch_f0_is_finite(config->yin_threshold) ||
        !fin_pitch_f0_is_finite(config->fallback_threshold) ||
        !fin_pitch_f0_is_finite(config->min_rms) ||
        config->min_hz <= 0.0f ||
        config->max_hz <= config->min_hz ||
        config->max_hz >= (float)sample_rate_hz * 0.5f ||
        config->yin_threshold <= 0.0f ||
        config->yin_threshold >= 1.0f ||
        config->fallback_threshold < config->yin_threshold ||
        config->fallback_threshold >= 1.0f ||
        config->min_rms < 0.0f) {
        return FIN_PITCH_F0_ERROR_INVALID_CONFIG;
    }

    for (i = 0u; i < frame_samples; ++i) {
        if (!fin_pitch_f0_is_finite(frame[i])) {
            return FIN_PITCH_F0_ERROR_NUMERIC_FAILURE;
        }
    }

    return FIN_PITCH_F0_OK;
}

/*
 * Convert frequency-derived lag bounds without ever converting an out-of-range
 * float to size_t. A caller can supply a finite, positive but extremely small
 * min_hz/max_hz; sample_rate / frequency can then overflow to infinity or exceed
 * SIZE_MAX. Lag search is frame-bounded anyway, so compare against the maximum
 * usable lag before conversion and clamp to that bound.
 */
static void fin_pitch_f0_compute_lag_bounds(
    size_t frame_samples,
    uint32_t sample_rate_hz,
    const FinPitchF0Config *config,
    size_t *min_lag_out,
    size_t *max_lag_out
)
{
    const size_t max_usable_lag = frame_samples - FIN_PITCH_F0_MIN_PAIRS;
    const float min_lag_estimate =
        (float)sample_rate_hz / config->max_hz;
    const float max_lag_estimate =
        (float)sample_rate_hz / config->min_hz;
    size_t min_lag;
    size_t max_lag;

    if (!isfinite(min_lag_estimate) ||
        min_lag_estimate >= (float)max_usable_lag) {
        min_lag = max_usable_lag;
    } else if (min_lag_estimate < 2.0f) {
        min_lag = 2u;
    } else {
        min_lag = (size_t)floorf(min_lag_estimate);
        if (min_lag < 2u) {
            min_lag = 2u;
        }
    }

    if (!isfinite(max_lag_estimate) ||
        max_lag_estimate >= (float)max_usable_lag) {
        max_lag = max_usable_lag;
    } else {
        max_lag = (size_t)ceilf(max_lag_estimate);
    }

    *min_lag_out = min_lag;
    *max_lag_out = max_lag;
}

FinPitchF0Status fin_pitch_f0_estimate(
    const float *frame,
    size_t frame_samples,
    uint32_t sample_rate_hz,
    const FinPitchF0Config *config,
    FinPitchF0Workspace *workspace,
    FinPitchF0Result *result
)
{
    FinPitchF0Result candidate_result = {0};
    size_t i;
    size_t min_lag;
    size_t max_lag;
    size_t best_lag = 0u;
    size_t selected_lag = 0u;
    float mean = 0.0f;
    float centered_energy = 0.0f;
    float running_difference = 0.0f;
    float best_cmnd = FLT_MAX;
    float frame_rms;

    const FinPitchF0Status input_status = fin_pitch_f0_validate(
        frame, frame_samples, sample_rate_hz, config, workspace, result
    );
    if (input_status != FIN_PITCH_F0_OK) {
        return input_status;
    }

    /* Mean removal is used for the RMS/voicing gate, not required in YIN deltas. */
    for (i = 0u; i < frame_samples; ++i) {
        mean += frame[i];
    }
    mean /= (float)frame_samples;

    for (i = 0u; i < frame_samples; ++i) {
        const float centered = frame[i] - mean;
        centered_energy += centered * centered;
    }
    frame_rms = sqrtf(centered_energy / (float)frame_samples);

    if (!fin_pitch_f0_is_finite(frame_rms)) {
        return FIN_PITCH_F0_ERROR_NUMERIC_FAILURE;
    }

    if (frame_rms < config->min_rms) {
        *result = candidate_result;
        return FIN_PITCH_F0_OK;
    }

    fin_pitch_f0_compute_lag_bounds(
        frame_samples,
        sample_rate_hz,
        config,
        &min_lag,
        &max_lag
    );

    if (min_lag >= max_lag) {
        return FIN_PITCH_F0_ERROR_INVALID_CONFIG;
    }

    /* YIN difference d(tau) = sum_j (x[j] - x[j+tau])^2. */
    workspace->difference_cmnd[0] = 0.0f;
    for (size_t lag = 1u; lag <= max_lag; ++lag) {
        float difference = 0.0f;
        const size_t pair_count = frame_samples - lag;

        for (i = 0u; i < pair_count; ++i) {
            /*
             * The centered terms cancel: (x[i] - mean) -
             * (x[i + lag] - mean) equals x[i] - x[i + lag]. Computing the
             * raw difference avoids two inner-loop subtractions and a temporary
             * per sample. Tiny floating-point rounding differences are expected.
             */
            const float delta = frame[i] - frame[i + lag];
            difference += delta * delta;
        }

        if (!fin_pitch_f0_is_finite(difference)) {
            return FIN_PITCH_F0_ERROR_NUMERIC_FAILURE;
        }
        workspace->difference_cmnd[lag] = difference;
    }

    /* Convert the difference function to cumulative mean normalized difference. */
    for (size_t lag = 1u; lag <= max_lag; ++lag) {
        const float difference = workspace->difference_cmnd[lag];
        running_difference += difference;

        if (!fin_pitch_f0_is_finite(running_difference)) {
            return FIN_PITCH_F0_ERROR_NUMERIC_FAILURE;
        }

        if (running_difference <= FLT_MIN) {
            workspace->difference_cmnd[lag] = 1.0f;
        } else {
            /* Divide first to avoid overflowing difference * lag needlessly. */
            workspace->difference_cmnd[lag] =
                (difference / running_difference) * (float)lag;
        }
    }

    candidate_result.lag_evaluations = (uint32_t)max_lag;

    /*
     * Prefer the first local minimum crossing the YIN threshold. This reduces
     * the risk of choosing a multiple of the fundamental period (an octave error).
     */
    for (size_t lag = min_lag; lag <= max_lag; ++lag) {
        const float value = workspace->difference_cmnd[lag];

        if (!fin_pitch_f0_is_finite(value)) {
            return FIN_PITCH_F0_ERROR_NUMERIC_FAILURE;
        }

        if (value < best_cmnd) {
            best_cmnd = value;
            best_lag = lag;
        }

        if (value < config->yin_threshold) {
            selected_lag = lag;
            while (selected_lag < max_lag &&
                   workspace->difference_cmnd[selected_lag + 1u] <
                       workspace->difference_cmnd[selected_lag]) {
                ++selected_lag;
            }
            break;
        }
    }

    /* No threshold crossing: accept only a convincing global minimum. */
    if (selected_lag == 0u && best_cmnd <= config->fallback_threshold) {
        selected_lag = best_lag;
    }

    if (selected_lag == 0u) {
        *result = candidate_result;
        return FIN_PITCH_F0_OK;
    }

    /* Parabolic interpolation refines the lag between discrete samples. */
    {
        float refined_lag = (float)selected_lag;
        const float center = workspace->difference_cmnd[selected_lag];

        if (selected_lag > min_lag && selected_lag < max_lag) {
            const float previous = workspace->difference_cmnd[selected_lag - 1u];
            const float next = workspace->difference_cmnd[selected_lag + 1u];
            const float denominator = previous - 2.0f * center + next;

            if (fin_pitch_f0_is_finite(denominator) && fabsf(denominator) > 1.0e-9f) {
                const float offset = fin_pitch_f0_clamp(
                    0.5f * (previous - next) / denominator,
                    -0.5f,
                    0.5f
                );
                refined_lag += offset;
            }
        }

        if (!fin_pitch_f0_is_finite(refined_lag) || refined_lag <= 0.0f) {
            return FIN_PITCH_F0_ERROR_NUMERIC_FAILURE;
        }

        candidate_result.period_samples = refined_lag;
        candidate_result.f0_hz = (float)sample_rate_hz / refined_lag;
        candidate_result.confidence = fin_pitch_f0_clamp(1.0f - center, 0.0f, 1.0f);
        candidate_result.selected_lag = (uint32_t)selected_lag;
        candidate_result.voiced = 1u;
    }

    if (!fin_pitch_f0_is_finite(candidate_result.f0_hz) ||
        candidate_result.f0_hz < config->min_hz * 0.95f ||
        candidate_result.f0_hz > config->max_hz * 1.05f) {
        candidate_result = (FinPitchF0Result){0};
        candidate_result.lag_evaluations = (uint32_t)max_lag;
    } else {
        candidate_result.f0_hz = fin_pitch_f0_clamp(
            candidate_result.f0_hz, config->min_hz, config->max_hz
        );
    }

    *result = candidate_result;
    return FIN_PITCH_F0_OK;
}
