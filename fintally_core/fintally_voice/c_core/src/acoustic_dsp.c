
/*
 * FINTALLY VOICE — ACOUSTIC DSP CORE
 *
 * C23 implementation. C/Rust FFI only.
 *
 * Responsibilities:
 *   - caller-owned DSP workspace
 *   - frame preprocessing and FFT/power spectrum
 *   - MFCC, LPC/formants, pitch, energy and ZCR orchestration
 *   - canonical 32-dimensional FP32 feature vector
 *   - PCM16/FP32 frame and streaming interfaces
 *
 * The scalar FP32 implementation is the reference acoustic feature path.
 * The UINT8 codebook kernels use a backend selected once during initialization.
 * Do not quantize the FP32 MFCC/LPC/F0 feature path merely to use integer SIMD:
 * that would alter trained feature semantics and require an explicit model-side
 * quantization contract.
 */

#include "acoustic_dsp.h"
#include "macros/cache_alignment.h"
#include "kernel/simd_dispatch.h"
#include "mfcc.h"
#include "lpc_formants.h"
#include "pitch_f0.h"
#include "energy_zcr.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FIN_TWO_PI_F 6.28318530717958647692f
#define FIN_FFT_SIZE 512u
#define FIN_FFT_BINS (FIN_FFT_SIZE / 2u + 1u)

/*
 * Private fixed-size state. It resides in caller-provided workspace during
 * normal operation; the realtime frame path does not allocate memory.
 */
typedef struct FinVoiceInternalState {
    /*
     * Private workspace fields are cache-line aligned because these arrays are
     * repeatedly read/written by the frame path. Alignment is internal to the
     * caller-owned workspace; it does not expose a private state layout to Rust.
     */
    FIN_VOICE_CACHE_ALIGN float input_frame[FIN_VOICE_FRAME_SAMPLES];
    FIN_VOICE_CACHE_ALIGN float windowed_frame[FIN_VOICE_FRAME_SAMPLES];

    FIN_VOICE_CACHE_ALIGN float fft_real[FIN_FFT_SIZE];
    FIN_VOICE_CACHE_ALIGN float fft_imag[FIN_FFT_SIZE];
    FIN_VOICE_CACHE_ALIGN float power[FIN_FFT_BINS];

    FIN_VOICE_CACHE_ALIGN float autocorrelation[FIN_LPC_ORDER + 1u];
    FIN_VOICE_CACHE_ALIGN float lpc_coefficients[FIN_LPC_ORDER];

    FIN_VOICE_CACHE_ALIGN float window[FIN_VOICE_FRAME_SAMPLES];
    FIN_VOICE_CACHE_ALIGN float twiddle_real[FIN_FFT_SIZE / 2u];
    FIN_VOICE_CACHE_ALIGN float twiddle_imag[FIN_FFT_SIZE / 2u];

    FIN_VOICE_CACHE_ALIGN FinMfccPlan mfcc_plan;
    FIN_VOICE_CACHE_ALIGN FinMfccWorkspace mfcc_workspace;

    FinPitchF0Config pitch_config;
    FIN_VOICE_CACHE_ALIGN FinPitchF0Workspace pitch_workspace;

    /* Initialized once; frame processing never repeats CPU feature detection. */
    FinVoiceSimdContext simd_context;

    FIN_VOICE_CACHE_ALIGN float stream_buffer[FIN_VOICE_FRAME_SAMPLES];
    size_t stream_count;
} FinVoiceInternalState;

/*
 * Keep one acoustic context's private state under the project's 100 KiB
 * software working-set target. This bounds owned DSP memory; it cannot promise
 * that every access hits L1 or avoids L2 on every microarchitecture.
 */
#define FIN_VOICE_ACOUSTIC_WORKSET_BUDGET_BYTES (100U * 1024U)
_Static_assert(
    sizeof(FinVoiceInternalState) <= FIN_VOICE_ACOUSTIC_WORKSET_BUDGET_BYTES,
    "Acoustic workspace exceeds the 100 KiB software working-set target; "
    "split large scratch into streaming tiles or review the budget explicitly."
);

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static inline float fin_clampf(
    float value,
    float min_value,
    float max_value
)
{
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}


static inline int fin_is_finite_f32(float value)
{
    return isfinite(value) ? 1 : 0;
}


/* Accept only backend identifiers defined by the shared dispatcher ABI. */
static int fin_backend_id_valid(FinVoiceSimdBackend backend)
{
    switch (backend) {
        case FIN_SIMD_AUTO:
        case FIN_SIMD_SCALAR:
        case FIN_SIMD_SSE42:
        case FIN_SIMD_AVX2:
        case FIN_SIMD_AVX2_VNNI:
        case FIN_SIMD_AVX512:
        case FIN_SIMD_NEON:
        case FIN_SIMD_ESP_DSP:
        case FIN_SIMD_AVX2_FMA:
            return 1;
        default:
            return 0;
    }
}


static FinVoiceInternalState *fin_internal_state(
    FinVoiceDspContext *context
)
{
    return (FinVoiceInternalState *)context->workspace.memory;
}


static FinVoiceDspStatus fin_map_lpc_formants_status(
    FinLpcFormantsStatus status
)
{
    switch (status) {
        case FIN_LPC_FORMANTS_OK:
            return FIN_DSP_OK;

        case FIN_LPC_FORMANTS_ERROR_INVALID_ARGUMENT:
            return FIN_DSP_ERROR_INVALID_ARGUMENT;

        case FIN_LPC_FORMANTS_ERROR_BUFFER_TOO_SMALL:
            return FIN_DSP_ERROR_BUFFER_TOO_SMALL;

        case FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE:
            return FIN_DSP_ERROR_NUMERIC_FAILURE;

        case FIN_LPC_FORMANTS_ERROR_INVALID_CONFIG:
        default:
            return FIN_DSP_ERROR_INVALID_CONFIG;
    }
}


static FinVoiceDspStatus fin_map_energy_zcr_status(
    FinEnergyZcrStatus status
)
{
    switch (status) {
        case FIN_ENERGY_ZCR_OK:
            return FIN_DSP_OK;

        case FIN_ENERGY_ZCR_ERROR_INVALID_ARGUMENT:
            return FIN_DSP_ERROR_INVALID_ARGUMENT;

        case FIN_ENERGY_ZCR_ERROR_BUFFER_TOO_SMALL:
            return FIN_DSP_ERROR_BUFFER_TOO_SMALL;

        case FIN_ENERGY_ZCR_ERROR_NUMERIC_FAILURE:
            return FIN_DSP_ERROR_NUMERIC_FAILURE;

        case FIN_ENERGY_ZCR_ERROR_INVALID_CONFIG:
        default:
            return FIN_DSP_ERROR_INVALID_CONFIG;
    }
}


/* -------------------------------------------------------------------------
 * Scalar FFT
 * ---------------------------------------------------------------------- */

static void fin_fft_forward(
    float *real,
    float *imag,
    const float *twiddle_real,
    const float *twiddle_imag
)
{
    size_t i;
    size_t j = 0u;
    size_t stage;

    /* Bit reversal. */
    for (i = 1u; i < FIN_FFT_SIZE; ++i) {
        size_t bit = FIN_FFT_SIZE >> 1u;

        while ((j & bit) != 0u) {
            j ^= bit;
            bit >>= 1u;
        }

        j ^= bit;

        if (i < j) {
            /*
             * This FFT has one caller, which zeroes the full imaginary input
             * immediately before entry. Reordering an all-zero imaginary
             * array cannot change its values, so only swap the real array.
             */
            const float real_tmp = real[i];

            real[i] = real[j];
            real[j] = real_tmp;
        }
    }

    /* Iterative radix-2 Cooley–Tukey stages. */
    for (stage = 1u; stage <= 9u; ++stage) {
        const size_t half = (size_t)1u << (stage - 1u);
        const size_t block = half << 1u;
        const size_t twiddle_step = FIN_FFT_SIZE / block;
        size_t base;

        for (base = 0u; base < FIN_FFT_SIZE; base += block) {
            size_t k;

            /*
             * k == 0 uses the identity twiddle (1, -0). Handle that
             * butterfly directly to avoid redundant twiddle multiplications.
             * The general butterfly below remains unchanged for k > 0.
             */
            {
                const size_t even = base;
                const size_t odd = base + half;
                const float odd_real = real[odd];
                const float odd_imag = imag[odd];
                const float even_real = real[even];
                const float even_imag = imag[even];

                real[even] = even_real + odd_real;
                imag[even] = even_imag + odd_imag;
                real[odd] = even_real - odd_real;
                imag[odd] = even_imag - odd_imag;
            }

            /* k = 0 was handled above; keep the general twiddle math for k > 0. */
            for (k = 1u; k < half; ++k) {
                const size_t twiddle_index = k * twiddle_step;
                const size_t even = base + k;
                const size_t odd = even + half;

                const float wr = twiddle_real[twiddle_index];
                const float wi = twiddle_imag[twiddle_index];

                const float odd_real = real[odd];
                const float odd_imag = imag[odd];

                const float tr = wr * odd_real - wi * odd_imag;
                const float ti = wr * odd_imag + wi * odd_real;

                const float er = real[even];
                const float ei = imag[even];

                real[even] = er + tr;
                imag[even] = ei + ti;
                real[odd] = er - tr;
                imag[odd] = ei - ti;
            }
        }
    }
}


/* -------------------------------------------------------------------------
 * Initialization tables
 * ---------------------------------------------------------------------- */

static FinVoiceDspStatus fin_initialize_tables(
    FinVoiceInternalState *state,
    const FinVoiceDspConfig *config
)
{
    size_t i;
    FinMfccStatus mfcc_status;

    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        const float phase =
            FIN_TWO_PI_F * (float)i /
            (float)(FIN_VOICE_FRAME_SAMPLES - 1u);

        state->window[i] = 0.5f - 0.5f * cosf(phase);
    }

    for (i = 0u; i < FIN_FFT_SIZE / 2u; ++i) {
        const float phase =
            -FIN_TWO_PI_F * (float)i / (float)FIN_FFT_SIZE;

        state->twiddle_real[i] = cosf(phase);
        state->twiddle_imag[i] = sinf(phase);
    }

    mfcc_status = fin_mfcc_init(
        &state->mfcc_plan,
        config->sample_rate_hz
    );

    if (mfcc_status != FIN_MFCC_OK) {
        return (mfcc_status == FIN_MFCC_ERROR_INVALID_ARGUMENT)
            ? FIN_DSP_ERROR_INVALID_ARGUMENT
            : FIN_DSP_ERROR_INVALID_CONFIG;
    }

    state->pitch_config = fin_pitch_f0_default_config();

    return FIN_DSP_OK;
}


/* -------------------------------------------------------------------------
 * PCM conversion and preprocessing
 * ---------------------------------------------------------------------- */

static void fin_pcm16_to_f32(
    const int16_t *input,
    float *output,
    size_t count
)
{
    size_t i;

    for (i = 0u; i < count; ++i) {
        output[i] = (float)input[i] / 32768.0f;
    }
}


static void fin_preprocess_frame(
    FinVoiceInternalState *state,
    const FinVoiceDspConfig *config
)
{
    size_t i;
    float previous = 0.0f;

    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        const float input = state->input_frame[i];
        const float emphasized =
            input - config->pre_emphasis * previous;

        previous = input;
        state->windowed_frame[i] = emphasized * state->window[i];
    }
}


/* -------------------------------------------------------------------------
 * Power spectrum
 * ---------------------------------------------------------------------- */

static void fin_compute_power_spectrum(
    FinVoiceInternalState *state
)
{
    size_t i;

    /*
     * The first 320 real elements are overwritten below. Only clear the
     * zero-padding tail; this reduces redundant writes in every frame.
     */
    memset(
        state->fft_real + FIN_VOICE_FRAME_SAMPLES,
        0,
        (FIN_FFT_SIZE - FIN_VOICE_FRAME_SAMPLES) *
            sizeof(state->fft_real[0])
    );

    memset(
        state->fft_imag,
        0,
        sizeof(state->fft_imag)
    );

    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        state->fft_real[i] = state->windowed_frame[i];
    }

    fin_fft_forward(
        state->fft_real,
        state->fft_imag,
        state->twiddle_real,
        state->twiddle_imag
    );

    for (i = 0u; i < FIN_FFT_BINS; ++i) {
        const float real = state->fft_real[i];
        const float imag = state->fft_imag[i];

        state->power[i] =
            (real * real + imag * imag) / (float)FIN_FFT_SIZE;
    }
}


/* -------------------------------------------------------------------------
 * Feature-family adapters
 * ---------------------------------------------------------------------- */

static FinVoiceDspStatus fin_compute_mfcc(
    FinVoiceInternalState *state,
    float *output,
    float epsilon
)
{
    const FinMfccStatus status = fin_mfcc_extract_power(
        &state->mfcc_plan,
        &state->mfcc_workspace,
        state->power,
        epsilon,
        output
    );

    switch (status) {
        case FIN_MFCC_OK:
            return FIN_DSP_OK;

        case FIN_MFCC_ERROR_INVALID_ARGUMENT:
            return FIN_DSP_ERROR_INVALID_ARGUMENT;

        case FIN_MFCC_ERROR_NUMERIC_FAILURE:
            return FIN_DSP_ERROR_NUMERIC_FAILURE;

        case FIN_MFCC_ERROR_NOT_INITIALIZED:
        case FIN_MFCC_ERROR_INVALID_CONFIG:
        default:
            return FIN_DSP_ERROR_INVALID_CONFIG;
    }
}


static FinVoiceDspStatus fin_compute_lpc(
    FinVoiceInternalState *state,
    size_t frame_samples,
    float epsilon
)
{
    FinLpcFormantsStatus status;

    status = fin_lpc_formants_autocorrelation(
        state->windowed_frame,
        frame_samples,
        state->autocorrelation
    );

    if (status != FIN_LPC_FORMANTS_OK) {
        return fin_map_lpc_formants_status(status);
    }

    status = fin_lpc_formants_compute_lpc(
        state->autocorrelation,
        epsilon,
        state->lpc_coefficients
    );

    return fin_map_lpc_formants_status(status);
}


static FinVoiceDspStatus fin_compute_formants(
    const FinVoiceInternalState *state,
    uint32_t sample_rate_hz,
    float formants[FIN_FORMANT_COUNT]
)
{
    return fin_map_lpc_formants_status(
        fin_lpc_formants_estimate(
            state->lpc_coefficients,
            sample_rate_hz,
            formants
        )
    );
}


static FinVoiceDspStatus fin_compute_pitch_f0(
    FinVoiceInternalState *state,
    const float *frame,
    size_t frame_samples,
    uint32_t sample_rate_hz,
    float *f0_hz
)
{
    FinPitchF0Result result = {0};
    FinPitchF0Status status;

    if (state == NULL || frame == NULL || f0_hz == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    status = fin_pitch_f0_estimate(
        frame,
        frame_samples,
        sample_rate_hz,
        &state->pitch_config,
        &state->pitch_workspace,
        &result
    );

    switch (status) {
        case FIN_PITCH_F0_OK:
            *f0_hz = result.voiced != 0u ? result.f0_hz : 0.0f;
            return FIN_DSP_OK;

        case FIN_PITCH_F0_ERROR_INVALID_ARGUMENT:
            return FIN_DSP_ERROR_INVALID_ARGUMENT;

        case FIN_PITCH_F0_ERROR_FRAME_TOO_LARGE:
            return FIN_DSP_ERROR_BUFFER_TOO_SMALL;

        case FIN_PITCH_F0_ERROR_INVALID_CONFIG:
            return FIN_DSP_ERROR_INVALID_CONFIG;

        case FIN_PITCH_F0_ERROR_NUMERIC_FAILURE:
        default:
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
    }
}


/* -------------------------------------------------------------------------
 * Canonical feature extraction
 * ---------------------------------------------------------------------- */

static FinVoiceDspStatus fin_extract_features_internal(
    FinVoiceDspContext *context,
    const float *samples,
    size_t sample_count,
    FinVoiceFeatureVector *output
)
{
    FinVoiceInternalState *state;
    const FinVoiceDspConfig *config;

    /* Short-lived feature staging also starts on the chosen cache boundary. */
    FIN_VOICE_CACHE_ALIGN float mfcc[FIN_MFCC_COUNT];
    FIN_VOICE_CACHE_ALIGN float formants[FIN_FORMANT_COUNT];
    float f0 = 0.0f;

    FinEnergyZcrFeatures energy_features;
    size_t i;

    if (context == NULL || samples == NULL || output == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    if (!context->initialized) {
        return FIN_DSP_ERROR_NOT_INITIALIZED;
    }

    if (sample_count != context->config.frame_samples) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    state = fin_internal_state(context);
    config = &context->config;

    memset(output->values, 0, sizeof(output->values));

    /*
     * The input may alias state->input_frame in the PCM16 wrapper. Reading
     * and writing the same index is safe; no later sample is overwritten.
     */
    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        if (!fin_is_finite_f32(samples[i])) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }

        state->input_frame[i] = fin_clampf(samples[i], -1.0f, 1.0f);
    }

    fin_preprocess_frame(state, config);
    fin_compute_power_spectrum(state);

    {
        const FinVoiceDspStatus status =
            fin_compute_mfcc(state, mfcc, config->epsilon);

        if (status != FIN_DSP_OK) {
            return status;
        }
    }

    {
        const FinVoiceDspStatus status = fin_compute_lpc(
            state,
            sample_count,
            config->epsilon
        );

        if (status != FIN_DSP_OK) {
            return status;
        }
    }

    {
        const FinVoiceDspStatus status = fin_compute_pitch_f0(
            state,
            state->input_frame,
            sample_count,
            config->sample_rate_hz,
            &f0
        );

        if (status != FIN_DSP_OK) {
            return status;
        }
    }

    {
        const FinVoiceDspStatus status = fin_compute_formants(
            state,
            config->sample_rate_hz,
            formants
        );

        if (status != FIN_DSP_OK) {
            return status;
        }
    }

    {
        const FinEnergyZcrStatus status = fin_energy_zcr_compute(
            state->input_frame,
            sample_count,
            state->power,
            FIN_FFT_BINS,
            FIN_FFT_SIZE,
            config->sample_rate_hz,
            config->epsilon,
            &energy_features
        );

        if (status != FIN_ENERGY_ZCR_OK) {
            return fin_map_energy_zcr_status(status);
        }
    }

    /* 0..12: MFCC. */
    for (i = 0u; i < FIN_MFCC_COUNT; ++i) {
        output->values[FIN_FEATURE_MFCC_0 + i] = mfcc[i];
    }

    /* 13..22: LPC. */
    for (i = 0u; i < FIN_LPC_ORDER; ++i) {
        output->values[FIN_FEATURE_LPC_0 + i] =
            state->lpc_coefficients[i];
    }

    /* 23..25: formants. */
    output->values[FIN_FEATURE_FORMANT_F1] = formants[0];
    output->values[FIN_FEATURE_FORMANT_F2] = formants[1];
    output->values[FIN_FEATURE_FORMANT_F3] = formants[2];

    output->values[FIN_FEATURE_F0] = f0;
    output->values[FIN_FEATURE_LOG_ENERGY] = energy_features.log_energy;
    output->values[FIN_FEATURE_ZCR] = energy_features.zero_crossing_rate;
    output->values[FIN_FEATURE_SPECTRAL_ENERGY] =
        energy_features.spectral_energy;
    output->values[FIN_FEATURE_SPECTRAL_CENTROID] =
        energy_features.spectral_centroid_hz;

    /* Dimension 31 is reserved and remains zero. */
    output->values[FIN_FEATURE_RESERVED_0] = 0.0f;

    return fin_voice_dsp_validate_features(output);
}


/* -------------------------------------------------------------------------
 * Configuration
 * ---------------------------------------------------------------------- */

void fin_voice_dsp_default_config(FinVoiceDspConfig *config)
{
    if (config == NULL) {
        return;
    }

    config->sample_rate_hz = FIN_VOICE_SAMPLE_RATE_HZ;
    config->channels = FIN_VOICE_CHANNELS;
    config->frame_samples = FIN_VOICE_FRAME_SAMPLES;
    config->hop_samples = FIN_VOICE_HOP_SAMPLES;
    config->pre_emphasis = 0.97f;
    config->epsilon = 1.0e-8f;
    config->simd_backend = FIN_SIMD_AUTO;
}


FinVoiceDspStatus fin_voice_dsp_validate_config(
    const FinVoiceDspConfig *config
)
{
    if (config == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    if (config->sample_rate_hz != FIN_VOICE_SAMPLE_RATE_HZ) {
        return FIN_DSP_ERROR_INVALID_SAMPLE_RATE;
    }

    if (config->channels != FIN_VOICE_CHANNELS) {
        return FIN_DSP_ERROR_INVALID_CHANNEL_COUNT;
    }

    if (config->frame_samples != FIN_VOICE_FRAME_SAMPLES ||
        config->hop_samples != FIN_VOICE_HOP_SAMPLES) {
        return FIN_DSP_ERROR_INVALID_CONFIG;
    }

    if (!fin_is_finite_f32(config->pre_emphasis) ||
        config->pre_emphasis < 0.0f ||
        config->pre_emphasis >= 1.0f) {
        return FIN_DSP_ERROR_INVALID_CONFIG;
    }

    if (!fin_is_finite_f32(config->epsilon) ||
        config->epsilon <= 0.0f) {
        return FIN_DSP_ERROR_INVALID_CONFIG;
    }

    if (!fin_backend_id_valid(config->simd_backend)) {
        return FIN_DSP_ERROR_INVALID_CONFIG;
    }

    return FIN_DSP_OK;
}


/* -------------------------------------------------------------------------
 * Workspace and initialization
 * ---------------------------------------------------------------------- */

size_t fin_voice_dsp_workspace_size(const FinVoiceDspConfig *config)
{
    const size_t alignment = _Alignof(FinVoiceInternalState);

    (void)config;

    if (alignment == 0u || alignment - 1u > SIZE_MAX -
        sizeof(FinVoiceInternalState)) {
        return 0u;
    }

    return sizeof(FinVoiceInternalState) + alignment - 1u;
}


FinVoiceDspStatus fin_voice_dsp_init(
    FinVoiceDspContext *context,
    const FinVoiceDspConfig *config,
    void *workspace_memory,
    size_t workspace_size
)
{
    const size_t alignment = _Alignof(FinVoiceInternalState);
    size_t required_size;
    size_t alignment_loss;
    uintptr_t raw_address;
    uintptr_t aligned_address;
    uintptr_t remainder;
    uintptr_t padding;
    FinVoiceInternalState *state;
    FinVoiceDspStatus config_status;
    FinVoiceSimdContext selected_simd = {0};

    if (context == NULL || config == NULL || workspace_memory == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    config_status = fin_voice_dsp_validate_config(config);
    if (config_status != FIN_DSP_OK) {
        return config_status;
    }

    /*
     * Resolve the backend exactly once at initialization. AUTO uses runtime
     * CPU/OS checks in the dispatcher and falls back to scalar. An explicitly
     * requested but unavailable backend is an error instead of a silent lie.
     */
    if (!fin_voice_simd_init(&selected_simd, config->simd_backend)) {
        return FIN_DSP_ERROR_UNSUPPORTED_BACKEND;
    }

    required_size = fin_voice_dsp_workspace_size(config);

    if (required_size == 0u || workspace_size < required_size) {
        return FIN_DSP_ERROR_WORKSPACE_TOO_SMALL;
    }

    if (alignment == 0u) {
        return FIN_DSP_ERROR_WORKSPACE_TOO_SMALL;
    }

    raw_address = (uintptr_t)workspace_memory;
    remainder = raw_address % (uintptr_t)alignment;
    padding = remainder == 0u
        ? 0u
        : (uintptr_t)alignment - remainder;

    if (raw_address > UINTPTR_MAX - padding) {
        return FIN_DSP_ERROR_WORKSPACE_TOO_SMALL;
    }

    aligned_address = raw_address + padding;
    alignment_loss = (size_t)padding;

    if (alignment_loss > workspace_size ||
        workspace_size - alignment_loss < sizeof(FinVoiceInternalState)) {
        return FIN_DSP_ERROR_WORKSPACE_TOO_SMALL;
    }

    memset(context, 0, sizeof(*context));

    context->config = *config;
    context->active_backend = fin_voice_simd_context_backend(&selected_simd);
    context->workspace.memory = (void *)aligned_address;
    context->workspace.memory_size = workspace_size - alignment_loss;

    state = fin_internal_state(context);
    memset(state, 0, sizeof(*state));
    state->simd_context = selected_simd;

    {
        const FinVoiceDspStatus status = fin_initialize_tables(state, config);

        if (status != FIN_DSP_OK) {
            context->initialized = false;
            context->workspace.memory = NULL;
            context->workspace.memory_size = 0u;
            return status;
        }
    }

    state->stream_count = 0u;
    context->initialized = true;

    return FIN_DSP_OK;
}


FinVoiceDspStatus fin_voice_dsp_reset(FinVoiceDspContext *context)
{
    FinVoiceInternalState *state;

    if (context == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    if (!context->initialized) {
        return FIN_DSP_ERROR_NOT_INITIALIZED;
    }

    state = fin_internal_state(context);
    state->stream_count = 0u;
    memset(state->stream_buffer, 0, sizeof(state->stream_buffer));

    return FIN_DSP_OK;
}


/* -------------------------------------------------------------------------
 * Backend information
 * ---------------------------------------------------------------------- */

FinVoiceSimdBackend fin_voice_dsp_active_backend(
    const FinVoiceDspContext *context
)
{
    if (context == NULL || !context->initialized) {
        return FIN_SIMD_SCALAR;
    }

    return context->active_backend;
}


const char *fin_voice_dsp_backend_name(FinVoiceSimdBackend backend)
{
    /* Keep backend naming in one place shared by dispatcher and diagnostics. */
    return fin_voice_simd_backend_name(backend);
}


/* -------------------------------------------------------------------------
 * Quantized codebook operations
 * ---------------------------------------------------------------------- */

static FinVoiceDspStatus fin_quantized_context(
    FinVoiceDspContext *context,
    FinVoiceInternalState **out_state)
{
    if (context == NULL || out_state == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }
    if (!context->initialized) {
        return FIN_DSP_ERROR_NOT_INITIALIZED;
    }

    *out_state = fin_internal_state(context);
    return FIN_DSP_OK;
}

FinVoiceDspStatus fin_voice_dsp_quantized_dot_u8(
    FinVoiceDspContext *context,
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    uint8_t zero_point,
    float *out_f32)
{
    FinVoiceInternalState *state;
    FinVoiceDspStatus status = fin_quantized_context(context, &state);

    if (status != FIN_DSP_OK) {
        return status;
    }
    if (out_f32 == NULL || (count != 0U && (lhs == NULL || rhs == NULL))) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(scale) || scale <= 0.0f) {
        return FIN_DSP_ERROR_INVALID_CONFIG;
    }
    if ((uintmax_t)count >
        ((uintmax_t)INT64_MAX / UINT64_C(65025))) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }
    return fin_voice_simd_dot_u8(
        &state->simd_context, lhs, rhs, count, scale, zero_point, out_f32)
        ? FIN_DSP_OK
        : FIN_DSP_ERROR_NUMERIC_FAILURE;
}

FinVoiceDspStatus fin_voice_dsp_quantized_squared_distance_u8(
    FinVoiceDspContext *context,
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    float *out_squared_distance_f32)
{
    FinVoiceInternalState *state;
    FinVoiceDspStatus status = fin_quantized_context(context, &state);

    if (status != FIN_DSP_OK) {
        return status;
    }
    if (out_squared_distance_f32 == NULL ||
        (count != 0U && (lhs == NULL || rhs == NULL))) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(scale) || scale <= 0.0f) {
        return FIN_DSP_ERROR_INVALID_CONFIG;
    }
    if ((uintmax_t)count >
        ((uintmax_t)UINT64_MAX / UINT64_C(65025))) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }
    return fin_voice_simd_squared_distance_u8(
        &state->simd_context, lhs, rhs, count, scale,
        out_squared_distance_f32)
        ? FIN_DSP_OK
        : FIN_DSP_ERROR_NUMERIC_FAILURE;
}

FinVoiceDspStatus fin_voice_dsp_quantized_nearest_u8(
    FinVoiceDspContext *context,
    const uint8_t *query,
    const uint8_t *centroids,
    size_t centroid_count,
    size_t dimension,
    float scale,
    size_t *out_index,
    float *out_squared_distance_f32)
{
    FinVoiceInternalState *state;
    FinVoiceDspStatus status = fin_quantized_context(context, &state);

    if (status != FIN_DSP_OK) {
        return status;
    }
    if (query == NULL || centroids == NULL || out_index == NULL ||
        out_squared_distance_f32 == NULL || centroid_count == 0U ||
        dimension == 0U) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }
    if (!isfinite(scale) || scale <= 0.0f) {
        return FIN_DSP_ERROR_INVALID_CONFIG;
    }
    if (centroid_count > SIZE_MAX / dimension ||
        (uintmax_t)dimension >
            ((uintmax_t)UINT64_MAX / UINT64_C(65025))) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }
    return fin_voice_simd_nearest_u8(
        &state->simd_context, query, centroids, centroid_count, dimension,
        scale, out_index, out_squared_distance_f32)
        ? FIN_DSP_OK
        : FIN_DSP_ERROR_NUMERIC_FAILURE;
}


/* -------------------------------------------------------------------------
 * Frame extraction
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_extract_features(
    FinVoiceDspContext *context,
    const int16_t *pcm16,
    size_t sample_count,
    FinVoiceFeatureVector *output
)
{
    FinVoiceInternalState *state;

    if (context == NULL || pcm16 == NULL || output == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    if (!context->initialized) {
        return FIN_DSP_ERROR_NOT_INITIALIZED;
    }

    if (sample_count != context->config.frame_samples) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    state = fin_internal_state(context);
    fin_pcm16_to_f32(pcm16, state->input_frame, sample_count);

    return fin_extract_features_internal(
        context,
        state->input_frame,
        sample_count,
        output
    );
}


FinVoiceDspStatus fin_voice_dsp_extract_features_f32(
    FinVoiceDspContext *context,
    const float *samples,
    size_t sample_count,
    FinVoiceFeatureVector *output
)
{
    return fin_extract_features_internal(
        context,
        samples,
        sample_count,
        output
    );
}


/* -------------------------------------------------------------------------
 * Normalization
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_normalize_features(
    const FinVoiceFeatureVector *input,
    const FinVoiceFeatureNormalization *normalization,
    FinVoiceFeatureVector *output
)
{
    size_t i;

    if (input == NULL || normalization == NULL || output == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    /*
     * Input/output aliasing is intentional and safe: each iteration reads
     * dimension i before writing the same output dimension.
     */
    for (i = 0u; i < FIN_VOICE_FEATURE_DIM; ++i) {
        const float value = input->values[i];
        const float mean = normalization->mean[i];
        const float inv_std = normalization->inv_std[i];

        if (!fin_is_finite_f32(value) ||
            !fin_is_finite_f32(mean) ||
            !fin_is_finite_f32(inv_std)) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }

        output->values[i] = (value - mean) * inv_std;

        if (!fin_is_finite_f32(output->values[i])) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }
    }

    return FIN_DSP_OK;
}


FinVoiceDspStatus fin_voice_dsp_extract_normalized(
    FinVoiceDspContext *context,
    const int16_t *pcm16,
    size_t sample_count,
    const FinVoiceFeatureNormalization *normalization,
    FinVoiceFeatureVector *output
)
{
    FinVoiceDspStatus status;

    if (normalization == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    status = fin_voice_dsp_extract_features(
        context,
        pcm16,
        sample_count,
        output
    );

    if (status != FIN_DSP_OK) {
        return status;
    }

    return fin_voice_dsp_normalize_features(output, normalization, output);
}


FinVoiceDspStatus fin_voice_dsp_extract_normalized_f32(
    FinVoiceDspContext *context,
    const float *samples,
    size_t sample_count,
    const FinVoiceFeatureNormalization *normalization,
    FinVoiceFeatureVector *output
)
{
    FinVoiceDspStatus status;

    if (normalization == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    status = fin_voice_dsp_extract_features_f32(
        context,
        samples,
        sample_count,
        output
    );

    if (status != FIN_DSP_OK) {
        return status;
    }

    return fin_voice_dsp_normalize_features(output, normalization, output);
}


/* -------------------------------------------------------------------------
 * Streaming helpers
 * ---------------------------------------------------------------------- */

static size_t fin_required_output_frames(
    size_t current_count,
    size_t incoming_count,
    size_t frame_size,
    size_t hop_size
)
{
    const size_t total = current_count + incoming_count;

    if (total < frame_size) {
        return 0u;
    }

    return 1u + (total - frame_size) / hop_size;
}


static void fin_advance_stream_after_frame(FinVoiceInternalState *state)
{
    memmove(
        state->stream_buffer,
        state->stream_buffer + FIN_VOICE_HOP_SAMPLES,
        (FIN_VOICE_FRAME_SAMPLES - FIN_VOICE_HOP_SAMPLES) *
            sizeof(state->stream_buffer[0])
    );

    state->stream_count =
        FIN_VOICE_FRAME_SAMPLES - FIN_VOICE_HOP_SAMPLES;
}


/* -------------------------------------------------------------------------
 * FP32 streaming
 * ---------------------------------------------------------------------- */

static FinVoiceDspStatus fin_push_f32_internal(
    FinVoiceDspContext *context,
    const float *samples,
    size_t sample_count,
    FinVoiceFeatureVector *outputs,
    size_t output_capacity,
    size_t *frames_produced
)
{
    FinVoiceInternalState *state;
    size_t required_frames;
    size_t produced = 0u;
    size_t input_index = 0u;
    size_t validation_index;

    if (context == NULL || samples == NULL || outputs == NULL ||
        frames_produced == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    *frames_produced = 0u;

    if (!context->initialized) {
        return FIN_DSP_ERROR_NOT_INITIALIZED;
    }

    state = fin_internal_state(context);

    if (sample_count > SIZE_MAX - state->stream_count) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    required_frames = fin_required_output_frames(
        state->stream_count,
        sample_count,
        context->config.frame_samples,
        context->config.hop_samples
    );

    if (required_frames > output_capacity) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    /*
     * Validate all input before changing persistent stream state. This avoids
     * partially consuming a block containing NaN or infinity.
     */
    for (validation_index = 0u;
         validation_index < sample_count;
         ++validation_index) {
        if (!fin_is_finite_f32(samples[validation_index])) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }
    }

    while (input_index < sample_count) {
        const size_t available =
            FIN_VOICE_FRAME_SAMPLES - state->stream_count;
        const size_t remaining = sample_count - input_index;
        const size_t copy_count =
            available < remaining ? available : remaining;
        size_t i;

        for (i = 0u; i < copy_count; ++i) {
            state->stream_buffer[state->stream_count + i] =
                fin_clampf(samples[input_index + i], -1.0f, 1.0f);
        }

        state->stream_count += copy_count;
        input_index += copy_count;

        if (state->stream_count == FIN_VOICE_FRAME_SAMPLES) {
            const FinVoiceDspStatus status =
                fin_voice_dsp_extract_features_f32(
                    context,
                    state->stream_buffer,
                    FIN_VOICE_FRAME_SAMPLES,
                    &outputs[produced]
                );

            if (status != FIN_DSP_OK) {
                /*
                 * Discard the failed frame while retaining the overlap. This
                 * prevents a full buffer from causing a zero-progress loop
                 * on a subsequent call.
                 */
                fin_advance_stream_after_frame(state);
                *frames_produced = produced;
                return status;
            }

            ++produced;
            fin_advance_stream_after_frame(state);
        }
    }

    *frames_produced = produced;
    return FIN_DSP_OK;
}


FinVoiceDspStatus fin_voice_dsp_push_f32(
    FinVoiceDspContext *context,
    const float *samples,
    size_t sample_count,
    FinVoiceFeatureVector *outputs,
    size_t output_capacity,
    size_t *frames_produced
)
{
    return fin_push_f32_internal(
        context,
        samples,
        sample_count,
        outputs,
        output_capacity,
        frames_produced
    );
}


/* -------------------------------------------------------------------------
 * PCM16 streaming
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_push_pcm16(
    FinVoiceDspContext *context,
    const int16_t *samples,
    size_t sample_count,
    FinVoiceFeatureVector *outputs,
    size_t output_capacity,
    size_t *frames_produced
)
{
    FinVoiceInternalState *state;
    size_t required_frames;
    size_t produced = 0u;
    size_t input_index = 0u;

    if (context == NULL || samples == NULL || outputs == NULL ||
        frames_produced == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    *frames_produced = 0u;

    if (!context->initialized) {
        return FIN_DSP_ERROR_NOT_INITIALIZED;
    }

    state = fin_internal_state(context);

    if (sample_count > SIZE_MAX - state->stream_count) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    required_frames = fin_required_output_frames(
        state->stream_count,
        sample_count,
        context->config.frame_samples,
        context->config.hop_samples
    );

    if (required_frames > output_capacity) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    while (input_index < sample_count) {
        const size_t available =
            FIN_VOICE_FRAME_SAMPLES - state->stream_count;
        const size_t remaining = sample_count - input_index;
        const size_t copy_count =
            available < remaining ? available : remaining;
        size_t i;

        for (i = 0u; i < copy_count; ++i) {
            state->stream_buffer[state->stream_count + i] =
                (float)samples[input_index + i] / 32768.0f;
        }

        state->stream_count += copy_count;
        input_index += copy_count;

        if (state->stream_count == FIN_VOICE_FRAME_SAMPLES) {
            const FinVoiceDspStatus status =
                fin_voice_dsp_extract_features_f32(
                    context,
                    state->stream_buffer,
                    FIN_VOICE_FRAME_SAMPLES,
                    &outputs[produced]
                );

            if (status != FIN_DSP_OK) {
                fin_advance_stream_after_frame(state);
                *frames_produced = produced;
                return status;
            }

            ++produced;
            fin_advance_stream_after_frame(state);
        }
    }

    *frames_produced = produced;
    return FIN_DSP_OK;
}


/* -------------------------------------------------------------------------
 * Flush partial stream frame
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_flush(
    FinVoiceDspContext *context,
    FinVoiceFeatureVector *outputs,
    size_t output_capacity,
    size_t *frames_produced
)
{
    FinVoiceInternalState *state;
    FinVoiceDspStatus status;

    if (context == NULL || outputs == NULL || frames_produced == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    *frames_produced = 0u;

    if (!context->initialized) {
        return FIN_DSP_ERROR_NOT_INITIALIZED;
    }

    state = fin_internal_state(context);

    if (state->stream_count == 0u) {
        return FIN_DSP_OK;
    }

    if (output_capacity == 0u) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    memset(
        state->stream_buffer + state->stream_count,
        0,
        (FIN_VOICE_FRAME_SAMPLES - state->stream_count) *
            sizeof(state->stream_buffer[0])
    );

    status = fin_voice_dsp_extract_features_f32(
        context,
        state->stream_buffer,
        FIN_VOICE_FRAME_SAMPLES,
        &outputs[0]
    );

    /*
     * A flush consumes the tail whether extraction succeeded or failed. Do
     * not leave a zero-padded frame in the persistent streaming buffer.
     */
    state->stream_count = 0u;
    *frames_produced = status == FIN_DSP_OK ? 1u : 0u;

    return status;
}


/* -------------------------------------------------------------------------
 * Reference state for standalone feature APIs
 * ---------------------------------------------------------------------- */

static FinVoiceDspStatus fin_create_reference_state(
    FinVoiceInternalState *state,
    uint32_t sample_rate_hz
)
{
    FinVoiceDspConfig config;

    memset(&config, 0, sizeof(config));

    config.sample_rate_hz = sample_rate_hz;
    config.channels = FIN_VOICE_CHANNELS;
    config.frame_samples = FIN_VOICE_FRAME_SAMPLES;
    config.hop_samples = FIN_VOICE_HOP_SAMPLES;
    config.pre_emphasis = 0.97f;
    config.epsilon = 1.0e-8f;
    config.simd_backend = FIN_SIMD_SCALAR;

    memset(state, 0, sizeof(*state));

    return fin_initialize_tables(state, &config);
}


/* -------------------------------------------------------------------------
 * Standalone MFCC API
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_mfcc(
    const float *frame,
    size_t frame_samples,
    uint32_t sample_rate_hz,
    float *mfcc_out,
    size_t mfcc_count
)
{
    FinVoiceInternalState state;
    FinVoiceDspConfig config;
    size_t i;
    FinVoiceDspStatus status;

    if (frame == NULL || mfcc_out == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    if (frame_samples != FIN_VOICE_FRAME_SAMPLES ||
        mfcc_count < FIN_MFCC_COUNT) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    if (sample_rate_hz == 0u) {
        return FIN_DSP_ERROR_INVALID_SAMPLE_RATE;
    }

    status = fin_create_reference_state(&state, sample_rate_hz);
    if (status != FIN_DSP_OK) {
        return status;
    }

    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        if (!fin_is_finite_f32(frame[i])) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }

        state.input_frame[i] = fin_clampf(frame[i], -1.0f, 1.0f);
    }

    fin_voice_dsp_default_config(&config);
    config.sample_rate_hz = sample_rate_hz;
    fin_preprocess_frame(&state, &config);
    fin_compute_power_spectrum(&state);

    status = fin_compute_mfcc(&state, mfcc_out, config.epsilon);
    if (status != FIN_DSP_OK) {
        return status;
    }

    for (i = 0u; i < FIN_MFCC_COUNT; ++i) {
        if (!fin_is_finite_f32(mfcc_out[i])) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }
    }

    return FIN_DSP_OK;
}


/* -------------------------------------------------------------------------
 * Standalone LPC API
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_lpc(
    const float *frame,
    size_t frame_samples,
    float *lpc_out,
    size_t lpc_count
)
{
    FinVoiceInternalState state;
    FinVoiceDspConfig config;
    FinVoiceDspStatus status;
    size_t i;

    if (frame == NULL || lpc_out == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    if (frame_samples != FIN_VOICE_FRAME_SAMPLES ||
        lpc_count < FIN_LPC_ORDER) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    fin_voice_dsp_default_config(&config);
    config.simd_backend = FIN_SIMD_SCALAR;

    status = fin_create_reference_state(&state, config.sample_rate_hz);
    if (status != FIN_DSP_OK) {
        return status;
    }

    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        if (!fin_is_finite_f32(frame[i])) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }

        state.input_frame[i] = fin_clampf(frame[i], -1.0f, 1.0f);
    }

    fin_preprocess_frame(&state, &config);

    status = fin_compute_lpc(&state, frame_samples, config.epsilon);
    if (status != FIN_DSP_OK) {
        return status;
    }

    memcpy(
        lpc_out,
        state.lpc_coefficients,
        FIN_LPC_ORDER * sizeof(lpc_out[0])
    );

    return FIN_DSP_OK;
}


/* -------------------------------------------------------------------------
 * Standalone formant API
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_formants(
    const float *frame,
    size_t frame_samples,
    uint32_t sample_rate_hz,
    float *formant_out,
    size_t formant_count
)
{
    FinVoiceInternalState state;
    FinVoiceDspConfig config;
    FinVoiceDspStatus status;
    size_t i;

    if (frame == NULL || formant_out == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    if (frame_samples != FIN_VOICE_FRAME_SAMPLES ||
        formant_count < FIN_FORMANT_COUNT) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    if (sample_rate_hz < 8000u) {
        return FIN_DSP_ERROR_INVALID_SAMPLE_RATE;
    }

    fin_voice_dsp_default_config(&config);
    config.sample_rate_hz = sample_rate_hz;
    config.simd_backend = FIN_SIMD_SCALAR;

    status = fin_create_reference_state(&state, sample_rate_hz);
    if (status != FIN_DSP_OK) {
        return status;
    }

    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        if (!fin_is_finite_f32(frame[i])) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }

        state.input_frame[i] = fin_clampf(frame[i], -1.0f, 1.0f);
    }

    fin_preprocess_frame(&state, &config);
    fin_compute_power_spectrum(&state);

    status = fin_compute_lpc(&state, frame_samples, config.epsilon);
    if (status != FIN_DSP_OK) {
        return status;
    }

    return fin_compute_formants(&state, sample_rate_hz, formant_out);
}


/* -------------------------------------------------------------------------
 * Standalone pitch API
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_pitch_f0(
    const float *frame,
    size_t frame_samples,
    uint32_t sample_rate_hz,
    float *f0_hz
)
{
    FinPitchF0Config config;
    FinPitchF0Workspace workspace = {0};
    FinPitchF0Result result = {0};
    FinPitchF0Status status;
    size_t i;

    if (frame == NULL || f0_hz == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    if (sample_rate_hz == 0u) {
        return FIN_DSP_ERROR_INVALID_SAMPLE_RATE;
    }

    /*
     * Reject oversized frames before inspecting frame[i]. The estimator uses
     * a fixed-size workspace; validating samples first could read beyond the
     * supported frame (and even report NaN instead of the size error).
     */
    if (frame_samples > FIN_PITCH_F0_MAX_FRAME_SAMPLES) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    for (i = 0u; i < frame_samples; ++i) {
        if (!fin_is_finite_f32(frame[i])) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }
    }

    config = fin_pitch_f0_default_config();

    status = fin_pitch_f0_estimate(
        frame,
        frame_samples,
        sample_rate_hz,
        &config,
        &workspace,
        &result
    );

    switch (status) {
        case FIN_PITCH_F0_OK:
            *f0_hz = result.voiced != 0u ? result.f0_hz : 0.0f;
            return FIN_DSP_OK;

        case FIN_PITCH_F0_ERROR_INVALID_ARGUMENT:
            return FIN_DSP_ERROR_INVALID_ARGUMENT;

        case FIN_PITCH_F0_ERROR_FRAME_TOO_LARGE:
            return FIN_DSP_ERROR_BUFFER_TOO_SMALL;

        case FIN_PITCH_F0_ERROR_INVALID_CONFIG:
            return FIN_DSP_ERROR_INVALID_CONFIG;

        case FIN_PITCH_F0_ERROR_NUMERIC_FAILURE:
        default:
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
    }
}


/* -------------------------------------------------------------------------
 * Standalone time-domain energy/ZCR API
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_energy_zcr(
    const float *frame,
    size_t frame_samples,
    float *log_energy,
    float *zcr
)
{
    FinEnergyZcrTimeFeatures result;
    FinEnergyZcrStatus status;
    size_t i;

    if (frame == NULL || log_energy == NULL || zcr == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    if (frame_samples != FIN_VOICE_FRAME_SAMPLES) {
        return FIN_DSP_ERROR_BUFFER_TOO_SMALL;
    }

    for (i = 0u; i < frame_samples; ++i) {
        if (!fin_is_finite_f32(frame[i])) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }
    }

    status = fin_energy_zcr_compute_time_domain(
        frame,
        frame_samples,
        1.0e-8f,
        &result
    );

    if (status != FIN_ENERGY_ZCR_OK) {
        return fin_map_energy_zcr_status(status);
    }

    *log_energy = result.log_energy;
    *zcr = result.zero_crossing_rate;

    return FIN_DSP_OK;
}


/* -------------------------------------------------------------------------
 * Feature validation and numerical comparison
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_validate_features(
    const FinVoiceFeatureVector *features
)
{
    size_t i;

    if (features == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    for (i = 0u; i < FIN_VOICE_FEATURE_DIM; ++i) {
        if (!fin_is_finite_f32(features->values[i])) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }
    }

    return FIN_DSP_OK;
}


FinVoiceDspStatus fin_voice_dsp_max_abs_error(
    const FinVoiceFeatureVector *reference,
    const FinVoiceFeatureVector *candidate,
    float *max_error
)
{
    float maximum = 0.0f;
    size_t i;

    if (reference == NULL || candidate == NULL || max_error == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    for (i = 0u; i < FIN_VOICE_FEATURE_DIM; ++i) {
        const float a = reference->values[i];
        const float b = candidate->values[i];

        if (!fin_is_finite_f32(a) || !fin_is_finite_f32(b)) {
            return FIN_DSP_ERROR_NUMERIC_FAILURE;
        }

        {
            const float error = fabsf(a - b);

            /* Finite operands can still overflow during subtraction. Do not
             * report success with an infinite error metric. */
            if (!fin_is_finite_f32(error)) {
                return FIN_DSP_ERROR_NUMERIC_FAILURE;
            }

            if (error > maximum) {
                maximum = error;
            }
        }
    }

    *max_error = maximum;

    return FIN_DSP_OK;
}


/* -------------------------------------------------------------------------
 * ABI and backend availability
 * ---------------------------------------------------------------------- */

uint32_t fin_voice_dsp_abi_version(void)
{
    return (FIN_VOICE_DSP_ABI_MAJOR << 16u) |
           FIN_VOICE_DSP_ABI_MINOR;
}


int fin_voice_dsp_backend_available(FinVoiceSimdBackend backend)
{
    return fin_voice_simd_backend_available(backend) ? 1 : 0;
}


/* -------------------------------------------------------------------------
 * DSP information
 * ---------------------------------------------------------------------- */

FinVoiceDspStatus fin_voice_dsp_get_info(
    const FinVoiceDspContext *context,
    FinVoiceDspInfo *info
)
{
    if (context == NULL || info == NULL) {
        return FIN_DSP_ERROR_INVALID_ARGUMENT;
    }

    if (!context->initialized) {
        return FIN_DSP_ERROR_NOT_INITIALIZED;
    }

    info->abi_major = FIN_VOICE_DSP_ABI_MAJOR;
    info->abi_minor = FIN_VOICE_DSP_ABI_MINOR;
    info->sample_rate_hz = context->config.sample_rate_hz;
    info->frame_samples = context->config.frame_samples;
    info->hop_samples = context->config.hop_samples;
    info->feature_dimension = FIN_VOICE_FEATURE_DIM;
    info->active_backend = context->active_backend;

    return FIN_DSP_OK;
}
