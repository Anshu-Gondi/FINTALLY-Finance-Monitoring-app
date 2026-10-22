#ifndef FINTALLY_VOICE_PITCH_F0_H
#define FINTALLY_VOICE_PITCH_F0_H

/*
 * FinTally Voice — pitch/F0 estimation interface.
 *
 * ISO C23, fixed-size caller-owned workspace, no heap allocation.
 * The estimator uses a YIN-style difference function and cumulative mean
 * normalized difference (CMND), followed by parabolic lag refinement.
 *
 * Input samples are finite float32 PCM-like values. The estimator removes the
 * frame mean for its RMS/voicing gate. The YIN difference function itself uses
 * raw sample differences because a common DC mean cancels algebraically.
 */

#include <stddef.h>
#include <stdint.h>

#define FIN_PITCH_F0_MAX_FRAME_SAMPLES 512u
#define FIN_PITCH_F0_DEFAULT_MIN_HZ 80.0f
#define FIN_PITCH_F0_DEFAULT_MAX_HZ 400.0f
#define FIN_PITCH_F0_DEFAULT_YIN_THRESHOLD 0.15f
#define FIN_PITCH_F0_DEFAULT_FALLBACK_THRESHOLD 0.35f
#define FIN_PITCH_F0_DEFAULT_MIN_RMS 1.0e-4f

/* Keep at least this many sample pairs for each tested lag. */
#define FIN_PITCH_F0_MIN_PAIRS 64u

typedef enum FinPitchF0Status {
    FIN_PITCH_F0_OK = 0,
    FIN_PITCH_F0_ERROR_INVALID_ARGUMENT = 1,
    FIN_PITCH_F0_ERROR_INVALID_CONFIG = 2,
    FIN_PITCH_F0_ERROR_FRAME_TOO_LARGE = 3,
    FIN_PITCH_F0_ERROR_NUMERIC_FAILURE = 4
} FinPitchF0Status;

typedef struct FinPitchF0Config {
    float min_hz;
    float max_hz;
    float yin_threshold;
    float fallback_threshold;
    float min_rms;
} FinPitchF0Config;

/*
 * Caller-owned scratch storage reused for each frame. Values hold the raw YIN
 * difference function, then the CMND values in place. Keep this public type
 * naturally aligned so its C/Rust FFI layout does not change. The acoustic DSP
 * private state aligns this member at the containing-field level. Standalone
 * callers may externally align a workspace object if measurements justify it;
 * 64-byte alignment is not required for correctness.
 */
typedef struct FinPitchF0Workspace {
    float difference_cmnd[FIN_PITCH_F0_MAX_FRAME_SAMPLES];
} FinPitchF0Workspace;

typedef struct FinPitchF0Result {
    float f0_hz;
    float period_samples;
    float confidence;
    uint32_t voiced;
    uint32_t selected_lag;
    uint32_t lag_evaluations; /* Difference-function lags computed (1..max_lag). */
} FinPitchF0Result;

FinPitchF0Config fin_pitch_f0_default_config(void);

/*
 * Estimate F0 for one frame. An unvoiced/silent frame returns FIN_PITCH_F0_OK
 * with voiced=0 and f0_hz=0. Non-finite input is rejected.
 *
 * Diagnostics:
 *   confidence = clamp(1 - CMND(selected_lag), 0, 1)
 *   lag_evaluations = number of evaluated candidate lags
 *
 * The caller must retain and reuse workspace; this routine does not allocate.
 */
FinPitchF0Status fin_pitch_f0_estimate(
    const float *frame,
    size_t frame_samples,
    uint32_t sample_rate_hz,
    const FinPitchF0Config *config,
    FinPitchF0Workspace *workspace,
    FinPitchF0Result *result
);

#endif /* FINTALLY_VOICE_PITCH_F0_H */
