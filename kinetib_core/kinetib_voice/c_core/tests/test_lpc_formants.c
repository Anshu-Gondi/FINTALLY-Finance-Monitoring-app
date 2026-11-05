#include "lpc_formants.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static size_t g_checks = 0u;
static size_t g_failures = 0u;

static void check_condition(int condition, const char *expression, int line)
{
    ++g_checks;
    if (condition == 0) {
        ++g_failures;
        (void)fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    }
}

#define CHECK(expression) check_condition((expression), #expression, __LINE__)

static int close_enough(float lhs, float rhs, float tolerance)
{
    return isfinite(lhs) && isfinite(rhs) && fabsf(lhs - rhs) <= tolerance;
}

static void test_autocorrelation(void)
{
    const float frame[16] = {
        0.25f, -0.50f, 0.75f, 0.125f, -0.25f, 0.5f, -0.125f, 0.875f,
        -0.75f, 0.375f, 0.625f, -0.375f, 0.25f, -0.125f, 0.5f, -0.25f
    };
    float actual[FIN_LPC_ORDER + 1u] = {0.0f};

    CHECK(fin_lpc_formants_autocorrelation(frame, 16u, actual) ==
          FIN_LPC_FORMANTS_OK);

    for (size_t lag = 0u; lag <= FIN_LPC_ORDER; ++lag) {
        float expected = 0.0f;
        for (size_t i = 0u; i < 16u - lag; ++i) {
            expected += frame[i] * frame[i + lag];
        }
        CHECK(close_enough(actual[lag], expected, 1.0e-6f));
    }
}

static void test_autocorrelation_errors_are_transactional(void)
{
    float frame[16] = {0.0f};
    float output[FIN_LPC_ORDER + 1u];

    for (size_t i = 0u; i <= FIN_LPC_ORDER; ++i) {
        output[i] = 123.25f;
    }
    CHECK(fin_lpc_formants_autocorrelation(NULL, 16u, output) ==
          FIN_LPC_FORMANTS_ERROR_INVALID_ARGUMENT);
    CHECK(fin_lpc_formants_autocorrelation(frame, 16u, NULL) ==
          FIN_LPC_FORMANTS_ERROR_INVALID_ARGUMENT);
    CHECK(fin_lpc_formants_autocorrelation(frame, FIN_LPC_ORDER, output) ==
          FIN_LPC_FORMANTS_ERROR_BUFFER_TOO_SMALL);

    frame[7] = NAN;
    CHECK(fin_lpc_formants_autocorrelation(frame, 16u, output) ==
          FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE);
    for (size_t i = 0u; i <= FIN_LPC_ORDER; ++i) {
        CHECK(output[i] == 123.25f);
    }
}

static void test_lpc_silence_and_bad_inputs(void)
{
    float correlation[FIN_LPC_ORDER + 1u] = {0.0f};
    float coefficients[FIN_LPC_ORDER];

    for (size_t i = 0u; i < FIN_LPC_ORDER; ++i) {
        coefficients[i] = 9.0f;
    }
    CHECK(fin_lpc_formants_compute_lpc(correlation, 1.0e-8f, coefficients) ==
          FIN_LPC_FORMANTS_OK);
    for (size_t i = 0u; i < FIN_LPC_ORDER; ++i) {
        CHECK(coefficients[i] == 0.0f);
    }

    for (size_t i = 0u; i < FIN_LPC_ORDER; ++i) {
        coefficients[i] = 17.0f;
    }
    CHECK(fin_lpc_formants_compute_lpc(correlation, 0.0f, coefficients) ==
          FIN_LPC_FORMANTS_ERROR_INVALID_CONFIG);
    for (size_t i = 0u; i < FIN_LPC_ORDER; ++i) {
        CHECK(coefficients[i] == 17.0f);
    }

    correlation[4] = INFINITY;
    CHECK(fin_lpc_formants_compute_lpc(correlation, 1.0e-8f, coefficients) ==
          FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE);
    for (size_t i = 0u; i < FIN_LPC_ORDER; ++i) {
        CHECK(coefficients[i] == 17.0f);
    }
}

static void test_formant_golden_outputs_and_ordering(void)
{
    const float no_resonance[FIN_LPC_ORDER] = {0.0f};
    const float one_peak[FIN_LPC_ORDER] = {
        0.1f, 0.2f, -0.3f, 0.4f, -0.2f, 0.1f, 0.05f, -0.15f, 0.08f, -0.04f
    };
    const float multiple_peaks[FIN_LPC_ORDER] = {
        -0.9f, 0.8f, -0.7f, 0.6f, -0.5f, 0.4f, -0.3f, 0.2f, -0.1f, 0.05f
    };
    float output[FIN_FORMANT_COUNT] = {-1.0f, -1.0f, -1.0f};

    CHECK(fin_lpc_formants_estimate(no_resonance, 16000u, output) ==
          FIN_LPC_FORMANTS_OK);
    CHECK(output[0] == 0.0f && output[1] == 0.0f && output[2] == 0.0f);

    CHECK(fin_lpc_formants_estimate(one_peak, 16000u, output) ==
          FIN_LPC_FORMANTS_OK);
    CHECK(output[0] == 1937.5f);
    CHECK(output[1] == 0.0f);
    CHECK(output[2] == 0.0f);

    CHECK(fin_lpc_formants_estimate(multiple_peaks, 16000u, output) ==
          FIN_LPC_FORMANTS_OK);
    CHECK(output[0] == 718.75f);
    CHECK(output[1] == 2125.0f);
    CHECK(output[2] == 0.0f);

    CHECK(output[0] <= output[1]);
    CHECK(output[1] <= output[2] || output[2] == 0.0f);
}

static void test_formant_failures_do_not_commit(void)
{
    float coefficients[FIN_LPC_ORDER] = {0.0f};
    float output[FIN_FORMANT_COUNT] = {777.0f, 888.0f, 999.0f};

    CHECK(fin_lpc_formants_estimate(NULL, 16000u, output) ==
          FIN_LPC_FORMANTS_ERROR_INVALID_ARGUMENT);
    CHECK(fin_lpc_formants_estimate(coefficients, 16000u, NULL) ==
          FIN_LPC_FORMANTS_ERROR_INVALID_ARGUMENT);
    CHECK(fin_lpc_formants_estimate(coefficients, 7999u, output) ==
          FIN_LPC_FORMANTS_ERROR_INVALID_CONFIG);
    CHECK(output[0] == 777.0f && output[1] == 888.0f && output[2] == 999.0f);

    coefficients[3] = NAN;
    CHECK(fin_lpc_formants_estimate(coefficients, 16000u, output) ==
          FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE);
    CHECK(output[0] == 777.0f && output[1] == 888.0f && output[2] == 999.0f);

    for (size_t i = 0u; i < FIN_LPC_ORDER; ++i) {
        coefficients[i] = FLT_MAX;
    }
    CHECK(fin_lpc_formants_estimate(coefficients, 16000u, output) ==
          FIN_LPC_FORMANTS_ERROR_NUMERIC_FAILURE);
    CHECK(output[0] == 777.0f && output[1] == 888.0f && output[2] == 999.0f);
}

static uint32_t random_state = UINT32_C(0x51C0FFEE);

static uint32_t next_random_u32(void)
{
    uint32_t value = random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value;
    return value;
}

static void test_formant_determinism_and_finite_outputs(void)
{
    const uint32_t sample_rates[] = {8000u, 16000u, 22050u, 44100u, 48000u};

    for (size_t test = 0u; test < 512u; ++test) {
        float coefficients[FIN_LPC_ORDER];
        float first[FIN_FORMANT_COUNT] = {0.0f};
        float second[FIN_FORMANT_COUNT] = {0.0f};
        const uint32_t sample_rate = sample_rates[
            test % (sizeof(sample_rates) / sizeof(sample_rates[0]))];
        const float amplitude = (test % 5u == 0u) ? 1.0f : 0.25f;

        for (size_t i = 0u; i < FIN_LPC_ORDER; ++i) {
            const int centered = (int)(next_random_u32() % 2001u) - 1000;
            coefficients[i] = ((float)centered / 1000.0f) * amplitude;
        }

        CHECK(fin_lpc_formants_estimate(coefficients, sample_rate, first) ==
              FIN_LPC_FORMANTS_OK);
        CHECK(fin_lpc_formants_estimate(coefficients, sample_rate, second) ==
              FIN_LPC_FORMANTS_OK);

        for (size_t i = 0u; i < FIN_FORMANT_COUNT; ++i) {
            CHECK(isfinite(first[i]));
            CHECK(first[i] >= 0.0f);
            CHECK(first[i] == second[i]);
            if (first[i] > 0.0f && i > 0u && first[i - 1u] > 0.0f) {
                CHECK(first[i - 1u] <= first[i]);
            }
        }
    }
}

int main(void)
{
    test_autocorrelation();
    test_autocorrelation_errors_are_transactional();
    test_lpc_silence_and_bad_inputs();
    test_formant_golden_outputs_and_ordering();
    test_formant_failures_do_not_commit();
    test_formant_determinism_and_finite_outputs();

    if (g_failures != 0u) {
        (void)fprintf(stderr, "LPC/formant tests: %zu/%zu checks failed\n",
                      g_failures, g_checks);
        return 1;
    }
    (void)printf("LPC/formant tests: %zu checks passed\n", g_checks);
    return 0;
}
