/*
 * FinTally Voice acoustic DSP tests.
 *
 * These tests deliberately exercise public APIs only. They validate numerical
 * invariants, input validation, caller-owned workspace alignment, streaming
 * state transitions, and repeatability. Cache-miss performance is measured by
 * the benchmark/perf workflow, not inferred from a passing unit test.
 */
#include "acoustic_dsp.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CACHE_LINE_BYTES 64u
#define TEST_TAU_F 6.28318530717958647692f

static unsigned long g_checks;
static unsigned long g_failures;

#define CHECK(condition) do { \
    ++g_checks; \
    if (!(condition)) { \
        ++g_failures; \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

typedef struct TestFixture {
    FinVoiceDspContext context;
    unsigned char *allocation;
    size_t workspace_size;
} TestFixture;

static int all_finite(const float *values, size_t count)
{
    size_t i;
    for (i = 0u; i < count; ++i) {
        if (!isfinite(values[i])) {
            return 0;
        }
    }
    return 1;
}

static void fill_sine_f32(float *frame, size_t count, float frequency_hz,
                          float amplitude, uint32_t sample_rate_hz)
{
    size_t i;
    for (i = 0u; i < count; ++i) {
        const float phase = TEST_TAU_F * frequency_hz * (float)i /
                            (float)sample_rate_hz;
        frame[i] = amplitude * sinf(phase);
    }
}

static void fill_sine_pcm16(int16_t *frame, size_t count, float frequency_hz,
                            float amplitude, uint32_t sample_rate_hz)
{
    size_t i;
    for (i = 0u; i < count; ++i) {
        const float phase = TEST_TAU_F * frequency_hz * (float)i /
                            (float)sample_rate_hz;
        const float sample = amplitude * sinf(phase);
        frame[i] = (int16_t)lrintf(sample * 32767.0f);
    }
}

static int fixture_init(TestFixture *fixture, FinVoiceSimdBackend backend)
{
    FinVoiceDspConfig config;
    size_t required;
    FinVoiceDspStatus status;

    memset(fixture, 0, sizeof(*fixture));
    fin_voice_dsp_default_config(&config);
    config.simd_backend = backend;

    required = fin_voice_dsp_workspace_size(&config);
    CHECK(required > 0u);
    if (required == 0u || required == SIZE_MAX) {
        return 0;
    }

    /* Offset the supplied base deliberately. init() must align its private
     * state inside caller-owned memory without changing the public ABI. */
    fixture->allocation = (unsigned char *)malloc(required + 1u);
    CHECK(fixture->allocation != NULL);
    if (fixture->allocation == NULL) {
        return 0;
    }

    fixture->workspace_size = required;
    status = fin_voice_dsp_init(&fixture->context, &config,
                                fixture->allocation + 1u, required);
    CHECK(status == FIN_DSP_OK);
    if (status != FIN_DSP_OK) {
        free(fixture->allocation);
        fixture->allocation = NULL;
        return 0;
    }

    CHECK(fixture->context.initialized);
    CHECK(fixture->context.workspace.memory != NULL);
    CHECK(((uintptr_t)fixture->context.workspace.memory %
           (uintptr_t)TEST_CACHE_LINE_BYTES) == 0u);
    CHECK(fin_voice_dsp_active_backend(&fixture->context) == backend);
    {
        FinVoiceDspInfo info = {0};
        CHECK(fin_voice_dsp_get_info(&fixture->context, &info) == FIN_DSP_OK);
        CHECK(info.abi_major == FIN_VOICE_DSP_ABI_MAJOR);
        CHECK(info.abi_minor == FIN_VOICE_DSP_ABI_MINOR);
        CHECK(info.sample_rate_hz == FIN_VOICE_SAMPLE_RATE_HZ);
        CHECK(info.frame_samples == FIN_VOICE_FRAME_SAMPLES);
        CHECK(info.hop_samples == FIN_VOICE_HOP_SAMPLES);
        CHECK(info.feature_dimension == FIN_VOICE_FEATURE_DIM);
        CHECK(info.active_backend == backend);
    }
    return 1;
}

static void fixture_destroy(TestFixture *fixture)
{
    if (fixture != NULL) {
        free(fixture->allocation);
        fixture->allocation = NULL;
        fixture->workspace_size = 0u;
        memset(&fixture->context, 0, sizeof(fixture->context));
    }
}

static void check_feature_vector(const FinVoiceFeatureVector *features)
{
    CHECK(fin_voice_dsp_validate_features(features) == FIN_DSP_OK);
    CHECK(all_finite(features->values, FIN_VOICE_FEATURE_DIM));
    CHECK(features->values[FIN_FEATURE_RESERVED_0] == 0.0f);
}

static void test_config_and_workspace(void)
{
    FinVoiceDspConfig config;
    FinVoiceDspContext context = {0};
    unsigned char *memory;
    size_t required;
    FinVoiceDspStatus status;

    fin_voice_dsp_default_config(&config);
    CHECK(fin_voice_dsp_validate_config(&config) == FIN_DSP_OK);
    required = fin_voice_dsp_workspace_size(&config);
    CHECK(required > 0u);

    config.sample_rate_hz = 8000u;
    CHECK(fin_voice_dsp_validate_config(&config) == FIN_DSP_ERROR_INVALID_SAMPLE_RATE);
    fin_voice_dsp_default_config(&config);
    config.channels = 2u;
    CHECK(fin_voice_dsp_validate_config(&config) == FIN_DSP_ERROR_INVALID_CHANNEL_COUNT);
    fin_voice_dsp_default_config(&config);
    config.pre_emphasis = NAN;
    CHECK(fin_voice_dsp_validate_config(&config) == FIN_DSP_ERROR_INVALID_CONFIG);
    fin_voice_dsp_default_config(&config);
    config.epsilon = 0.0f;
    CHECK(fin_voice_dsp_validate_config(&config) == FIN_DSP_ERROR_INVALID_CONFIG);
    fin_voice_dsp_default_config(&config);
    config.simd_backend = (FinVoiceSimdBackend)999;
    CHECK(fin_voice_dsp_validate_config(&config) == FIN_DSP_ERROR_INVALID_CONFIG);

    fin_voice_dsp_default_config(&config);
    required = fin_voice_dsp_workspace_size(&config);
    memory = (unsigned char *)malloc(required + 1u);
    CHECK(memory != NULL);
    if (memory != NULL) {
        status = fin_voice_dsp_init(&context, &config, memory + 1u,
                                    required - 1u);
        CHECK(status == FIN_DSP_ERROR_WORKSPACE_TOO_SMALL);
        free(memory);
    }

    CHECK(fin_voice_dsp_init(NULL, &config, NULL, required) ==
          FIN_DSP_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_dsp_init(&context, NULL, NULL, required) ==
          FIN_DSP_ERROR_INVALID_ARGUMENT);
    CHECK(fin_voice_dsp_abi_version() ==
          ((FIN_VOICE_DSP_ABI_MAJOR << 16u) | FIN_VOICE_DSP_ABI_MINOR));
    CHECK(fin_voice_dsp_backend_available(FIN_SIMD_AUTO) != 0);
    CHECK(fin_voice_dsp_backend_available(FIN_SIMD_SCALAR) != 0);
    CHECK(strcmp(fin_voice_dsp_backend_name(FIN_SIMD_SCALAR), "scalar") == 0);
}

static void test_frame_extraction(TestFixture *fixture)
{
    int16_t pcm[FIN_VOICE_FRAME_SAMPLES];
    float frame[FIN_VOICE_FRAME_SAMPLES];
    float offset_storage[FIN_VOICE_FRAME_SAMPLES + 1u];
    FinVoiceFeatureVector output;
    FinVoiceFeatureVector repeated;
    struct GuardedOutput {
        uint32_t before;
        FinVoiceFeatureVector vector;
        uint32_t after;
    } guarded;
    size_t i;
    float max_error = -1.0f;

    memset(pcm, 0, sizeof(pcm));
    memset(&output, 0xA5, sizeof(output));
    CHECK(fin_voice_dsp_extract_features(&fixture->context, pcm,
                                         FIN_VOICE_FRAME_SAMPLES, &output) == FIN_DSP_OK);
    check_feature_vector(&output);
    CHECK(output.values[FIN_FEATURE_F0] == 0.0f);

    fill_sine_f32(frame, FIN_VOICE_FRAME_SAMPLES, 200.0f, 0.25f,
                  FIN_VOICE_SAMPLE_RATE_HZ);
    CHECK(fin_voice_dsp_extract_features_f32(&fixture->context, frame,
                                              FIN_VOICE_FRAME_SAMPLES,
                                              &output) == FIN_DSP_OK);
    check_feature_vector(&output);
    repeated = output;
    CHECK(fin_voice_dsp_extract_features_f32(&fixture->context, frame,
                                              FIN_VOICE_FRAME_SAMPLES,
                                              &output) == FIN_DSP_OK);
    CHECK(fin_voice_dsp_max_abs_error(&repeated, &output, &max_error) == FIN_DSP_OK);
    CHECK(max_error == 0.0f);

    /* Input pointers need natural float alignment, not a 64-byte boundary. */
    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        offset_storage[i + 1u] = frame[i];
    }
    CHECK(fin_voice_dsp_extract_features_f32(&fixture->context,
                                              offset_storage + 1u,
                                              FIN_VOICE_FRAME_SAMPLES,
                                              &output) == FIN_DSP_OK);
    check_feature_vector(&output);

    /* Finite values outside the normalized range are clamped by contract. */
    for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
        frame[i] = (i & 1u) != 0u ? 2.0f : -2.0f;
    }
    CHECK(fin_voice_dsp_extract_features_f32(&fixture->context, frame,
                                              FIN_VOICE_FRAME_SAMPLES,
                                              &output) == FIN_DSP_OK);
    check_feature_vector(&output);

    frame[123] = NAN;
    CHECK(fin_voice_dsp_extract_features_f32(&fixture->context, frame,
                                              FIN_VOICE_FRAME_SAMPLES,
                                              &output) == FIN_DSP_ERROR_NUMERIC_FAILURE);
    fill_sine_f32(frame, FIN_VOICE_FRAME_SAMPLES, 180.0f, 0.2f,
                  FIN_VOICE_SAMPLE_RATE_HZ);
    CHECK(fin_voice_dsp_extract_features_f32(&fixture->context, frame,
                                              FIN_VOICE_FRAME_SAMPLES,
                                              &output) == FIN_DSP_OK);
    check_feature_vector(&output);

    CHECK(fin_voice_dsp_extract_features_f32(&fixture->context, frame,
                                              FIN_VOICE_FRAME_SAMPLES - 1u,
                                              &output) == FIN_DSP_ERROR_BUFFER_TOO_SMALL);
    CHECK(fin_voice_dsp_extract_features_f32(NULL, frame,
                                              FIN_VOICE_FRAME_SAMPLES,
                                              &output) == FIN_DSP_ERROR_INVALID_ARGUMENT);

    guarded.before = UINT32_C(0x1234ABCD);
    guarded.after = UINT32_C(0xDCBA4321);
    CHECK(fin_voice_dsp_extract_features_f32(&fixture->context, frame,
                                              FIN_VOICE_FRAME_SAMPLES,
                                              &guarded.vector) == FIN_DSP_OK);
    CHECK(guarded.before == UINT32_C(0x1234ABCD));
    CHECK(guarded.after == UINT32_C(0xDCBA4321));
}

static void test_normalization(void)
{
    FinVoiceFeatureVector input;
    FinVoiceFeatureVector output;
    FinVoiceFeatureVector extreme_a;
    FinVoiceFeatureVector extreme_b;
    FinVoiceFeatureNormalization normalization;
    float max_error = -1.0f;
    size_t i;

    for (i = 0u; i < FIN_VOICE_FEATURE_DIM; ++i) {
        input.values[i] = (float)i * 0.25f - 2.0f;
        normalization.mean[i] = 0.0f;
        normalization.inv_std[i] = 1.0f;
    }

    CHECK(fin_voice_dsp_normalize_features(&input, &normalization, &output) == FIN_DSP_OK);
    CHECK(fin_voice_dsp_max_abs_error(&input, &output, &max_error) == FIN_DSP_OK);
    CHECK(max_error == 0.0f);

    /* The API explicitly permits in-place operation. */
    CHECK(fin_voice_dsp_normalize_features(&input, &normalization, &input) == FIN_DSP_OK);
    CHECK(fin_voice_dsp_max_abs_error(&output, &input, &max_error) == FIN_DSP_OK);
    CHECK(max_error == 0.0f);

    for (i = 0u; i < FIN_VOICE_FEATURE_DIM; ++i) {
        extreme_a.values[i] = FLT_MAX;
        extreme_b.values[i] = -FLT_MAX;
    }
    CHECK(fin_voice_dsp_max_abs_error(&extreme_a, &extreme_b, &max_error) ==
          FIN_DSP_ERROR_NUMERIC_FAILURE);

    normalization.inv_std[7] = INFINITY;
    CHECK(fin_voice_dsp_normalize_features(&output, &normalization, &input) ==
          FIN_DSP_ERROR_NUMERIC_FAILURE);
    CHECK(fin_voice_dsp_normalize_features(NULL, &normalization, &input) ==
          FIN_DSP_ERROR_INVALID_ARGUMENT);
}

static void test_individual_kernels(void)
{
    float frame[FIN_VOICE_FRAME_SAMPLES];
    float mfcc[16] = {0.0f};
    float lpc[16] = {0.0f};
    float formants[4] = {0.0f};
    float f0 = -1.0f;
    float log_energy = 0.0f;
    float zcr = -1.0f;
    size_t i;

    fill_sine_f32(frame, FIN_VOICE_FRAME_SAMPLES, 200.0f, 0.25f,
                  FIN_VOICE_SAMPLE_RATE_HZ);

    CHECK(fin_voice_dsp_mfcc(frame, FIN_VOICE_FRAME_SAMPLES,
                             FIN_VOICE_SAMPLE_RATE_HZ, mfcc, 13u) == FIN_DSP_OK);
    CHECK(all_finite(mfcc, 13u));
    CHECK(fin_voice_dsp_mfcc(frame, FIN_VOICE_FRAME_SAMPLES - 1u,
                             FIN_VOICE_SAMPLE_RATE_HZ, mfcc, 13u) ==
          FIN_DSP_ERROR_BUFFER_TOO_SMALL);
    CHECK(fin_voice_dsp_lpc(frame, FIN_VOICE_FRAME_SAMPLES, lpc, 10u) == FIN_DSP_OK);
    CHECK(all_finite(lpc, 10u));
    CHECK(fin_voice_dsp_formants(frame, FIN_VOICE_FRAME_SAMPLES,
                                 FIN_VOICE_SAMPLE_RATE_HZ, formants, 3u) == FIN_DSP_OK);
    CHECK(all_finite(formants, 3u));
    CHECK(fin_voice_dsp_pitch_f0(frame, FIN_VOICE_FRAME_SAMPLES,
                                 FIN_VOICE_SAMPLE_RATE_HZ, &f0) == FIN_DSP_OK);
    CHECK(isfinite(f0));
    CHECK(f0 >= 0.0f);
    CHECK(fin_voice_dsp_energy_zcr(frame, FIN_VOICE_FRAME_SAMPLES,
                                   &log_energy, &zcr) == FIN_DSP_OK);
    CHECK(isfinite(log_energy));
    CHECK(isfinite(zcr));
    CHECK(zcr >= 0.0f && zcr <= 1.0f);

    /* A too-large F0 frame is rejected by length before examining its data. */
    {
        float oversized[513];
        for (i = 0u; i < sizeof(oversized) / sizeof(oversized[0]); ++i) {
            oversized[i] = 0.0f;
        }
        oversized[512] = NAN;
        CHECK(fin_voice_dsp_pitch_f0(oversized, 513u,
                                     FIN_VOICE_SAMPLE_RATE_HZ, &f0) ==
              FIN_DSP_ERROR_BUFFER_TOO_SMALL);
    }

    frame[0] = NAN;
    CHECK(fin_voice_dsp_mfcc(frame, FIN_VOICE_FRAME_SAMPLES,
                             FIN_VOICE_SAMPLE_RATE_HZ, mfcc, 13u) ==
          FIN_DSP_ERROR_NUMERIC_FAILURE);
}

static void test_streaming(TestFixture *fixture)
{
    int16_t pcm[3u * FIN_VOICE_HOP_SAMPLES];
    FinVoiceFeatureVector stream_outputs[2];
    FinVoiceFeatureVector reference;
    size_t produced = SIZE_MAX;
    size_t i;
    float error = -1.0f;

    fill_sine_pcm16(pcm, sizeof(pcm) / sizeof(pcm[0]), 220.0f, 0.2f,
                    FIN_VOICE_SAMPLE_RATE_HZ);

    /* Failed capacity preflight must not consume any samples. */
    CHECK(fin_voice_dsp_reset(&fixture->context) == FIN_DSP_OK);
    CHECK(fin_voice_dsp_push_pcm16(&fixture->context, pcm,
                                   sizeof(pcm) / sizeof(pcm[0]),
                                   stream_outputs, 1u, &produced) ==
          FIN_DSP_ERROR_BUFFER_TOO_SMALL);
    CHECK(produced == 0u);

    CHECK(fin_voice_dsp_push_pcm16(&fixture->context, pcm,
                                   sizeof(pcm) / sizeof(pcm[0]),
                                   stream_outputs, 2u, &produced) == FIN_DSP_OK);
    CHECK(produced == 2u);
    check_feature_vector(&stream_outputs[0]);
    check_feature_vector(&stream_outputs[1]);

    CHECK(fin_voice_dsp_extract_features(&fixture->context, pcm,
                                          FIN_VOICE_FRAME_SAMPLES,
                                          &reference) == FIN_DSP_OK);
    CHECK(fin_voice_dsp_max_abs_error(&stream_outputs[0], &reference, &error) == FIN_DSP_OK);
    CHECK(error <= 1.0e-6f);
    CHECK(fin_voice_dsp_extract_features(&fixture->context,
                                          pcm + FIN_VOICE_HOP_SAMPLES,
                                          FIN_VOICE_FRAME_SAMPLES,
                                          &reference) == FIN_DSP_OK);
    CHECK(fin_voice_dsp_max_abs_error(&stream_outputs[1], &reference, &error) == FIN_DSP_OK);
    CHECK(error <= 1.0e-6f);

    /* A partial tail can be flushed once; the second flush produces no frame. */
    CHECK(fin_voice_dsp_reset(&fixture->context) == FIN_DSP_OK);
    produced = SIZE_MAX;
    CHECK(fin_voice_dsp_push_pcm16(&fixture->context, pcm, 100u,
                                   stream_outputs, 2u, &produced) == FIN_DSP_OK);
    CHECK(produced == 0u);
    CHECK(fin_voice_dsp_flush(&fixture->context, stream_outputs, 0u,
                              &produced) == FIN_DSP_ERROR_BUFFER_TOO_SMALL);
    CHECK(produced == 0u);
    CHECK(fin_voice_dsp_flush(&fixture->context, stream_outputs, 1u,
                              &produced) == FIN_DSP_OK);
    CHECK(produced == 1u);
    check_feature_vector(&stream_outputs[0]);
    {
        float expected_tail[FIN_VOICE_FRAME_SAMPLES] = {0.0f};
        for (i = 0u; i < 100u; ++i) {
            expected_tail[i] = (float)pcm[i] / 32768.0f;
        }
        CHECK(fin_voice_dsp_extract_features_f32(&fixture->context,
                                                  expected_tail,
                                                  FIN_VOICE_FRAME_SAMPLES,
                                                  &reference) == FIN_DSP_OK);
        CHECK(fin_voice_dsp_max_abs_error(&stream_outputs[0], &reference,
                                          &error) == FIN_DSP_OK);
        CHECK(error <= 1.0e-6f);
    }
    CHECK(fin_voice_dsp_flush(&fixture->context, stream_outputs, 1u,
                              &produced) == FIN_DSP_OK);
    CHECK(produced == 0u);

    /* F32 streaming validates the entire incoming block before state changes. */
    {
        float partial[FIN_VOICE_HOP_SAMPLES];
        float invalid[FIN_VOICE_HOP_SAMPLES];
        for (i = 0u; i < FIN_VOICE_HOP_SAMPLES; ++i) {
            partial[i] = 0.1f;
            invalid[i] = 0.1f;
        }
        invalid[FIN_VOICE_HOP_SAMPLES - 1u] = NAN;
        CHECK(fin_voice_dsp_reset(&fixture->context) == FIN_DSP_OK);
        CHECK(fin_voice_dsp_push_f32(&fixture->context, partial,
                                     FIN_VOICE_HOP_SAMPLES, stream_outputs,
                                     2u, &produced) == FIN_DSP_OK);
        CHECK(produced == 0u);
        CHECK(fin_voice_dsp_push_f32(&fixture->context, invalid,
                                     FIN_VOICE_HOP_SAMPLES, stream_outputs,
                                     2u, &produced) == FIN_DSP_ERROR_NUMERIC_FAILURE);
        CHECK(produced == 0u);
        CHECK(fin_voice_dsp_push_f32(&fixture->context, partial,
                                     FIN_VOICE_HOP_SAMPLES, stream_outputs,
                                     2u, &produced) == FIN_DSP_OK);
        CHECK(produced == 1u);
        check_feature_vector(&stream_outputs[0]);
        {
            float expected_frame[FIN_VOICE_FRAME_SAMPLES];
            for (i = 0u; i < FIN_VOICE_FRAME_SAMPLES; ++i) {
                expected_frame[i] = 0.1f;
            }
            CHECK(fin_voice_dsp_extract_features_f32(&fixture->context,
                                                      expected_frame,
                                                      FIN_VOICE_FRAME_SAMPLES,
                                                      &reference) == FIN_DSP_OK);
            CHECK(fin_voice_dsp_max_abs_error(&stream_outputs[0], &reference,
                                              &error) == FIN_DSP_OK);
            CHECK(error <= 1.0e-6f);
        }
    }
}

static void test_repeated_processing(TestFixture *fixture)
{
    float frame[FIN_VOICE_FRAME_SAMPLES];
    FinVoiceFeatureVector output;
    unsigned iteration;
    float checksum = 0.0f;

    fill_sine_f32(frame, FIN_VOICE_FRAME_SAMPLES, 173.0f, 0.17f,
                  FIN_VOICE_SAMPLE_RATE_HZ);
    for (iteration = 0u; iteration < 2048u; ++iteration) {
        const FinVoiceDspStatus status = fin_voice_dsp_extract_features_f32(
            &fixture->context, frame, FIN_VOICE_FRAME_SAMPLES, &output);
        if (status != FIN_DSP_OK) {
            CHECK(status == FIN_DSP_OK);
            return;
        }
        if (!all_finite(output.values, FIN_VOICE_FEATURE_DIM) ||
            output.values[FIN_FEATURE_RESERVED_0] != 0.0f) {
            CHECK(all_finite(output.values, FIN_VOICE_FEATURE_DIM));
            CHECK(output.values[FIN_FEATURE_RESERVED_0] == 0.0f);
            return;
        }
        checksum += output.values[FIN_FEATURE_MFCC_0];
    }
    CHECK(isfinite(checksum));
}

int main(void)
{
    TestFixture fixture;
    FinVoiceDspConfig report_config;
    size_t reported_workspace_size;

    fin_voice_dsp_default_config(&report_config);
    reported_workspace_size = fin_voice_dsp_workspace_size(&report_config);
    (void)printf("Acoustic workspace request: %zu bytes\n", reported_workspace_size);

    test_config_and_workspace();
    test_normalization();
    test_individual_kernels();

    if (fixture_init(&fixture, FIN_SIMD_SCALAR)) {
        test_frame_extraction(&fixture);
        test_streaming(&fixture);
        test_repeated_processing(&fixture);
        fixture_destroy(&fixture);
    }

    if (g_failures != 0u) {
        (void)fprintf(stderr, "Acoustic DSP tests: %lu/%lu checks failed\n",
                      g_failures, g_checks);
        return EXIT_FAILURE;
    }

    (void)printf("Acoustic DSP tests: %lu checks passed\n", g_checks);
    return EXIT_SUCCESS;
}
