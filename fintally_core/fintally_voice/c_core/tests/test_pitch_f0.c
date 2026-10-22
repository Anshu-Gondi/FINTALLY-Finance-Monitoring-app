#include "pitch_f0.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_SAMPLE_RATE 16000u
#define TEST_FRAME_SAMPLES 320u
#define TEST_PI 3.14159265358979323846f

static unsigned int checks;
static unsigned int failures;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        (void)fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
    } \
} while (0)

static void fill_sine(float *frame, size_t count, float frequency, float phase)
{
    for (size_t i = 0u; i < count; ++i) {
        const float angle =
            (2.0f * TEST_PI * frequency * (float)i / (float)TEST_SAMPLE_RATE) + phase;
        frame[i] = 0.7f * sinf(angle);
    }
}

static void test_sine_frequencies(void)
{
    static const float frequencies[] = {100.0f, 150.0f, 220.0f, 300.0f, 390.0f};
    float frame[TEST_FRAME_SAMPLES];
    FinPitchF0Workspace workspace = {0};
    FinPitchF0Config config = fin_pitch_f0_default_config();

    for (size_t f = 0u; f < sizeof(frequencies) / sizeof(frequencies[0]); ++f) {
        FinPitchF0Result result = {0};
        fill_sine(frame, TEST_FRAME_SAMPLES, frequencies[f], 0.17f);
        const FinPitchF0Status status = fin_pitch_f0_estimate(
            frame, TEST_FRAME_SAMPLES, TEST_SAMPLE_RATE,
            &config, &workspace, &result);
        CHECK(status == FIN_PITCH_F0_OK, "sine input returns OK");
        CHECK(result.voiced == 1u, "sine input is voiced");
        CHECK(isfinite(result.f0_hz), "estimated F0 is finite");
        CHECK(fabsf(result.f0_hz - frequencies[f]) <= frequencies[f] * 0.08f,
              "estimated F0 within 8 percent of generated sine");
        CHECK(result.lag_evaluations > 0u, "lag diagnostics are populated for non-silence");
    }
}

static void test_silence_and_invalid_inputs(void)
{
    float silence[TEST_FRAME_SAMPLES] = {0.0f};
    float frame[TEST_FRAME_SAMPLES] = {0.0f};
    float too_large[FIN_PITCH_F0_MAX_FRAME_SAMPLES + 1u] = {0.0f};
    FinPitchF0Workspace workspace = {0};
    FinPitchF0Config config = fin_pitch_f0_default_config();
    FinPitchF0Result result = {0};

    CHECK(fin_pitch_f0_estimate(silence, TEST_FRAME_SAMPLES, TEST_SAMPLE_RATE,
          &config, &workspace, &result) == FIN_PITCH_F0_OK,
          "silence returns OK");
    CHECK(result.voiced == 0u && result.f0_hz == 0.0f,
          "silence returns unvoiced zero F0");

    frame[17] = NAN;
    CHECK(fin_pitch_f0_estimate(frame, TEST_FRAME_SAMPLES, TEST_SAMPLE_RATE,
          &config, &workspace, &result) == FIN_PITCH_F0_ERROR_NUMERIC_FAILURE,
          "non-finite sample rejected");

    CHECK(fin_pitch_f0_estimate(too_large, sizeof(too_large) / sizeof(too_large[0]),
          TEST_SAMPLE_RATE, &config, &workspace, &result) ==
          FIN_PITCH_F0_ERROR_FRAME_TOO_LARGE,
          "oversized frame rejected before scanning samples");

    CHECK(fin_pitch_f0_estimate(frame, TEST_FRAME_SAMPLES, TEST_SAMPLE_RATE,
          &config, NULL, &result) == FIN_PITCH_F0_ERROR_INVALID_ARGUMENT,
          "null workspace rejected");

    config.min_hz = 1.0e-30f;
    config.max_hz = 2.0e-30f;
    fill_sine(frame, TEST_FRAME_SAMPLES, 220.0f, 0.0f);
    CHECK(fin_pitch_f0_estimate(frame, TEST_FRAME_SAMPLES, TEST_SAMPLE_RATE,
          &config, &workspace, &result) == FIN_PITCH_F0_ERROR_INVALID_CONFIG,
          "unusable frequency range is rejected without unsafe float-to-integer conversion");
}

static void test_repeatability_and_dc_offset(void)
{
    float frame_a[TEST_FRAME_SAMPLES];
    float frame_b[TEST_FRAME_SAMPLES];
    FinPitchF0Workspace workspace_a = {0};
    FinPitchF0Workspace workspace_b = {0};
    FinPitchF0Config config = fin_pitch_f0_default_config();
    FinPitchF0Result result_a = {0};
    FinPitchF0Result result_b = {0};

    fill_sine(frame_a, TEST_FRAME_SAMPLES, 220.0f, 0.0f);
    for (size_t i = 0u; i < TEST_FRAME_SAMPLES; ++i) {
        frame_b[i] = frame_a[i] + 0.25f;
    }

    CHECK(fin_pitch_f0_estimate(frame_a, TEST_FRAME_SAMPLES, TEST_SAMPLE_RATE,
          &config, &workspace_a, &result_a) == FIN_PITCH_F0_OK,
          "base tone returns OK");
    CHECK(fin_pitch_f0_estimate(frame_a, TEST_FRAME_SAMPLES, TEST_SAMPLE_RATE,
          &config, &workspace_a, &result_b) == FIN_PITCH_F0_OK,
          "repeated tone returns OK");
    CHECK(result_a.f0_hz == result_b.f0_hz &&
          result_a.selected_lag == result_b.selected_lag &&
          result_a.confidence == result_b.confidence,
          "same input produces deterministic result");

    CHECK(fin_pitch_f0_estimate(frame_b, TEST_FRAME_SAMPLES, TEST_SAMPLE_RATE,
          &config, &workspace_b, &result_b) == FIN_PITCH_F0_OK,
          "DC-offset tone returns OK");
    CHECK(result_b.voiced == 1u, "DC-offset tone remains voiced");
    CHECK(fabsf(result_b.f0_hz - result_a.f0_hz) < 1.0f,
          "DC offset does not materially change F0");
}

int main(void)
{
    test_sine_frequencies();
    test_silence_and_invalid_inputs();
    test_repeatability_and_dc_offset();

    (void)printf("Pitch F0 tests: %u checks, %u failures\n", checks, failures);
    return failures == 0u ? EXIT_SUCCESS : EXIT_FAILURE;
}
