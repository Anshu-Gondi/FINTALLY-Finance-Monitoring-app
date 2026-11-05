/*
 * FinTally Voice — MFCC module regression tests (ISO C23).
 *
 * Intent
 * ------
 * Keep the MFCC implementation auditable by testing its public contract rather
 * than its private helper functions. These tests cover:
 *   - 64-byte type/member/object alignment and the documented working-set cap;
 *   - plan initialization, Mel-bin invariants, and orthonormal DCT-II rows;
 *   - silence, flat, impulse, deterministic varied, and large finite spectra;
 *   - public status codes and preservation of caller output on failure;
 *   - repeated calls using reusable caller-owned workspace;
 *   - output aliasing the workspace's log-Mel array, as documented in mfcc.c.
 *
 * The reference calculation below is intentionally plain scalar C. It is not
 * a production implementation. Golden checks (the canonical 16 kHz bin table,
 * DCT orthonormality, and silence behavior) provide independent invariants so
 * that a matching reference loop alone cannot mask every class of regression.
 *
 * Tests allocate memory at startup where useful. This does not imply or permit
 * heap allocation in the MFCC frame-extraction path itself.
 */
#include "mfcc.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_EPSILON_F 1.0e-8f
#define TEST_ABS_TOL_F 2.0e-5f
#define TEST_CACHE_LINE_BYTES 64u
#define TEST_WORKING_SET_BUDGET_BYTES (100u * 1024u)

static unsigned long g_checks;
static unsigned long g_failures;

#define CHECK(condition) do { \
    ++g_checks; \
    if (!(condition)) { \
        ++g_failures; \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", \
                      __FILE__, __LINE__, #condition); \
    } \
} while (0)

#define CHECK_NEAR(actual, expected, tolerance) do { \
    const float check_actual_ = (actual); \
    const float check_expected_ = (expected); \
    const float check_tolerance_ = (tolerance); \
    ++g_checks; \
    if (!isfinite(check_actual_) || !isfinite(check_expected_) || \
        fabsf(check_actual_ - check_expected_) > check_tolerance_) { \
        ++g_failures; \
        (void)fprintf(stderr, \
            "FAIL %s:%d: |%.9g - %.9g| <= %.9g\n", \
            __FILE__, __LINE__, (double)check_actual_, \
            (double)check_expected_, (double)check_tolerance_); \
    } \
} while (0)

_Static_assert(_Alignof(FinMfccPlan) >= TEST_CACHE_LINE_BYTES,
               "FinMfccPlan must preserve the documented cache alignment");
_Static_assert(_Alignof(FinMfccWorkspace) >= TEST_CACHE_LINE_BYTES,
               "FinMfccWorkspace must preserve the documented cache alignment");
_Static_assert(sizeof(float) == 4u, "The MFCC contract requires 32-bit float storage");
_Static_assert((sizeof(FinMfccPlan) % _Alignof(FinMfccPlan)) == 0u,
               "FinMfccPlan size must be a multiple of its alignment");
_Static_assert((sizeof(FinMfccWorkspace) % _Alignof(FinMfccWorkspace)) == 0u,
               "FinMfccWorkspace size must be a multiple of its alignment");
_Static_assert(
    sizeof(FinMfccPlan) + sizeof(FinMfccWorkspace) +
        sizeof(float) * FIN_MFCC_FFT_BINS +
        sizeof(float) * FIN_MFCC_COUNT <= TEST_WORKING_SET_BUDGET_BYTES,
    "MFCC tables and operation scratch exceed the software working-set budget");

static int floats_are_finite(const float *values, size_t count)
{
    size_t i;
    for (i = 0u; i < count; ++i) {
        if (!isfinite(values[i])) {
            return 0;
        }
    }
    return 1;
}

static int floats_equal_exact(const float *lhs, const float *rhs, size_t count)
{
    size_t i;
    for (i = 0u; i < count; ++i) {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return 1;
}

static void fill_power_constant(float power[FIN_MFCC_FFT_BINS], float value)
{
    size_t i;
    for (i = 0u; i < FIN_MFCC_FFT_BINS; ++i) {
        power[i] = value;
    }
}

static void fill_power_deterministic(float power[FIN_MFCC_FFT_BINS])
{
    /* Unsigned wraparound is defined by C, making this data portable/repeatable. */
    uint32_t state = UINT32_C(0x4D464343);
    size_t i;

    for (i = 0u; i < FIN_MFCC_FFT_BINS; ++i) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        power[i] = (float)(state >> 8u) / 16777215.0f;
    }
}

/* Independent plain-C reference for the public filterbank/DCT contract. */
static void reference_log_mel(
    const FinMfccPlan *plan,
    const float power[FIN_MFCC_FFT_BINS],
    float epsilon,
    float output[FIN_MEL_FILTER_COUNT])
{
    size_t m;

    for (m = 0u; m < FIN_MEL_FILTER_COUNT; ++m) {
        const size_t left = (size_t)plan->mel_bins[m];
        const size_t center = (size_t)plan->mel_bins[m + 1u];
        const size_t right = (size_t)plan->mel_bins[m + 2u];
        float energy = 0.0f;
        size_t bin;

        if (center > left) {
            for (bin = left; bin < center && bin < FIN_MFCC_FFT_BINS; ++bin) {
                const float weight =
                    (float)(bin - left) / (float)(center - left);
                energy += power[bin] * weight;
            }
        }
        if (right > center) {
            for (bin = center;
                 bin <= right && bin < FIN_MFCC_FFT_BINS;
                 ++bin) {
                const float weight =
                    (float)(right - bin) / (float)(right - center);
                energy += power[bin] * weight;
            }
        }
        output[m] = logf(fmaxf(energy, epsilon));
    }
}

static void reference_mfcc(
    const FinMfccPlan *plan,
    const float power[FIN_MFCC_FFT_BINS],
    float epsilon,
    float output[FIN_MFCC_COUNT])
{
    float log_mel[FIN_MEL_FILTER_COUNT];
    size_t coefficient;

    reference_log_mel(plan, power, epsilon, log_mel);
    for (coefficient = 0u; coefficient < FIN_MFCC_COUNT; ++coefficient) {
        float sum = 0.0f;
        size_t band;
        for (band = 0u; band < FIN_MEL_FILTER_COUNT; ++band) {
            sum += log_mel[band] *
                plan->dct[coefficient * FIN_MEL_FILTER_COUNT + band];
        }
        output[coefficient] = sum;
    }
}

static void check_output_unchanged(
    const float *actual,
    const float *before,
    size_t count)
{
    CHECK(floats_equal_exact(actual, before, count));
}

static void check_successful_case(
    const FinMfccPlan *plan,
    FinMfccWorkspace *workspace,
    const float power[FIN_MFCC_FFT_BINS],
    float epsilon)
{
    float actual_log_mel[FIN_MEL_FILTER_COUNT];
    float expected_log_mel[FIN_MEL_FILTER_COUNT];
    float actual_mfcc[FIN_MFCC_COUNT];
    float expected_mfcc[FIN_MFCC_COUNT];
    FinMfccStatus status;

    reference_log_mel(plan, power, epsilon, expected_log_mel);
    status = fin_mfcc_extract_log_mel_power(
        plan, workspace, power, epsilon, actual_log_mel);
    CHECK(status == FIN_MFCC_OK);
    if (status != FIN_MFCC_OK) {
        return;
    }
    CHECK(floats_are_finite(actual_log_mel, FIN_MEL_FILTER_COUNT));
    for (size_t i = 0u; i < FIN_MEL_FILTER_COUNT; ++i) {
        CHECK_NEAR(actual_log_mel[i], expected_log_mel[i], TEST_ABS_TOL_F);
    }

    reference_mfcc(plan, power, epsilon, expected_mfcc);
    status = fin_mfcc_extract_power(
        plan, workspace, power, epsilon, actual_mfcc);
    CHECK(status == FIN_MFCC_OK);
    if (status != FIN_MFCC_OK) {
        return;
    }
    CHECK(floats_are_finite(actual_mfcc, FIN_MFCC_COUNT));
    for (size_t i = 0u; i < FIN_MFCC_COUNT; ++i) {
        CHECK_NEAR(actual_mfcc[i], expected_mfcc[i], TEST_ABS_TOL_F);
    }
}

static void test_alignment_and_initialization(void)
{
    FinMfccPlan plan = {0};
    FinMfccWorkspace workspace = {0};
    static const uint16_t expected_16khz_bins[FIN_MEL_FILTER_COUNT + 2u] = {
        0u, 2u, 4u, 7u, 10u, 13u, 16u, 20u, 24u, 29u,
        34u, 40u, 46u, 53u, 60u, 68u, 77u, 87u, 97u, 109u,
        122u, 136u, 152u, 169u, 188u, 209u, 231u, 256u
    };
    static const uint32_t supported_rates[] = {8000u, 16000u, 44100u, 48000u};
    size_t i;

    CHECK(((uintptr_t)&plan % TEST_CACHE_LINE_BYTES) == 0u);
    CHECK(((uintptr_t)&workspace % TEST_CACHE_LINE_BYTES) == 0u);
    CHECK((offsetof(FinMfccPlan, dct) % TEST_CACHE_LINE_BYTES) == 0u);
    CHECK((offsetof(FinMfccPlan, mel_bins) % TEST_CACHE_LINE_BYTES) == 0u);
    CHECK((offsetof(FinMfccWorkspace, log_mel) % TEST_CACHE_LINE_BYTES) == 0u);
    CHECK(((uintptr_t)&plan.dct[0] % TEST_CACHE_LINE_BYTES) == 0u);
    CHECK(((uintptr_t)&plan.mel_bins[0] % TEST_CACHE_LINE_BYTES) == 0u);
    CHECK(((uintptr_t)&workspace.log_mel[0] % TEST_CACHE_LINE_BYTES) == 0u);

    /* Pin the intentionally aligned public layout. Change these assertions
     * only alongside an explicit C/Rust ABI review. */
    CHECK(offsetof(FinMfccPlan, sample_rate_hz) == 0u);
    CHECK(offsetof(FinMfccPlan, dct) == 64u);
    CHECK(offsetof(FinMfccPlan, mel_bins) == 1472u);
    CHECK(offsetof(FinMfccPlan, initialized) == 1528u);
    CHECK(sizeof(FinMfccPlan) == 1536u);
    CHECK(offsetof(FinMfccWorkspace, log_mel) == 0u);
    CHECK(sizeof(FinMfccWorkspace) == 128u);

    CHECK(fin_mfcc_init(NULL, 16000u) == FIN_MFCC_ERROR_INVALID_ARGUMENT);
    CHECK(fin_mfcc_init(&plan, 16000u) == FIN_MFCC_OK);
    CHECK(fin_mfcc_init(&plan, 0u) == FIN_MFCC_ERROR_INVALID_CONFIG);
    CHECK(plan.initialized == 0u);
    CHECK(plan.sample_rate_hz == 0u);

    for (i = 0u; i < sizeof(supported_rates) / sizeof(supported_rates[0]); ++i) {
        const uint32_t rate = supported_rates[i];
        size_t bin;

        CHECK(fin_mfcc_init(&plan, rate) == FIN_MFCC_OK);
        CHECK(plan.initialized == 1u);
        CHECK(plan.sample_rate_hz == rate);
        CHECK(plan.mel_bins[0] == 0u);
        CHECK(plan.mel_bins[FIN_MEL_FILTER_COUNT + 1u] ==
              FIN_MFCC_FFT_BINS - 1u);
        CHECK(floats_are_finite(plan.dct,
              FIN_MFCC_COUNT * FIN_MEL_FILTER_COUNT));

        for (bin = 0u; bin < FIN_MEL_FILTER_COUNT + 2u; ++bin) {
            CHECK(plan.mel_bins[bin] < FIN_MFCC_FFT_BINS);
            if (bin != 0u) {
                CHECK(plan.mel_bins[bin] >= plan.mel_bins[bin - 1u]);
            }
        }

        /* Preserve the exact historical 16 kHz Mel-bin mapping. */
        if (rate == 16000u) {
            for (bin = 0u; bin < FIN_MEL_FILTER_COUNT + 2u; ++bin) {
                CHECK(plan.mel_bins[bin] == expected_16khz_bins[bin]);
            }
        }

        /* An orthonormal DCT-II has pairwise orthonormal rows. */
        for (size_t row_a = 0u; row_a < FIN_MFCC_COUNT; ++row_a) {
            for (size_t row_b = 0u; row_b < FIN_MFCC_COUNT; ++row_b) {
                float dot = 0.0f;
                size_t k;
                for (k = 0u; k < FIN_MEL_FILTER_COUNT; ++k) {
                    dot += plan.dct[row_a * FIN_MEL_FILTER_COUNT + k] *
                           plan.dct[row_b * FIN_MEL_FILTER_COUNT + k];
                }
                CHECK_NEAR(dot, row_a == row_b ? 1.0f : 0.0f, 2.0e-5f);
            }
        }
    }

    /* Test aligned dynamic allocation without assuming malloc alignment
     * exceeds max_align_t. Both sizes are multiples of the requested alignment. */
    {
        FinMfccPlan *heap_plan = (FinMfccPlan *)aligned_alloc(
            _Alignof(FinMfccPlan), sizeof(FinMfccPlan));
        FinMfccWorkspace *heap_workspace = (FinMfccWorkspace *)aligned_alloc(
            _Alignof(FinMfccWorkspace), sizeof(FinMfccWorkspace));
        CHECK(heap_plan != NULL);
        CHECK(heap_workspace != NULL);
        if (heap_plan != NULL) {
            CHECK(((uintptr_t)heap_plan % _Alignof(FinMfccPlan)) == 0u);
            CHECK(fin_mfcc_init(heap_plan, 16000u) == FIN_MFCC_OK);
        }
        if (heap_workspace != NULL) {
            CHECK(((uintptr_t)heap_workspace % _Alignof(FinMfccWorkspace)) == 0u);
        }
        free(heap_plan);
        free(heap_workspace);
    }
}

static void test_known_spectra(void)
{
    FinMfccPlan plan = {0};
    FinMfccWorkspace workspace = {0};
    float power[FIN_MFCC_FFT_BINS];
    float log_mel[FIN_MEL_FILTER_COUNT];
    float coefficients[FIN_MFCC_COUNT];
    const float silence_log = logf(TEST_EPSILON_F);

    CHECK(fin_mfcc_init(&plan, 16000u) == FIN_MFCC_OK);

    /* Silence: every filter floors at epsilon, producing one nonzero DC MFCC
     * and near-zero higher coefficients (finite-precision DCT residual only). */
    fill_power_constant(power, 0.0f);
    CHECK(fin_mfcc_extract_log_mel_power(
        &plan, &workspace, power, TEST_EPSILON_F, log_mel) == FIN_MFCC_OK);
    for (size_t i = 0u; i < FIN_MEL_FILTER_COUNT; ++i) {
        CHECK_NEAR(log_mel[i], silence_log, 1.0e-6f);
    }
    CHECK(fin_mfcc_extract_power(
        &plan, &workspace, power, TEST_EPSILON_F, coefficients) == FIN_MFCC_OK);
    CHECK_NEAR(coefficients[0], sqrtf((float)FIN_MEL_FILTER_COUNT) * silence_log,
               5.0e-5f);
    for (size_t i = 1u; i < FIN_MFCC_COUNT; ++i) {
        CHECK_NEAR(coefficients[i], 0.0f, 5.0e-5f);
    }

    /* Constant positive spectrum; verifies finite logs and DCT output. */
    fill_power_constant(power, 1.0f);
    check_successful_case(&plan, &workspace, power, TEST_EPSILON_F);

    /* Deterministic, non-flat input catches accidental band/order transposition. */
    fill_power_deterministic(power);
    check_successful_case(&plan, &workspace, power, TEST_EPSILON_F);

    /* Single-bin impulses exercise endpoints and bins on either side of filters. */
    {
        static const size_t impulse_bins[] = {0u, 1u, 64u, 128u, 255u, 256u};
        for (size_t item = 0u;
             item < sizeof(impulse_bins) / sizeof(impulse_bins[0]);
             ++item) {
            fill_power_constant(power, 0.0f);
            power[impulse_bins[item]] = 1.0f;
            check_successful_case(&plan, &workspace, power, TEST_EPSILON_F);
        }
    }

    /* Large finite power must remain usable when the weighted sums stay in
     * range; validation should reject non-finite results, not large values. */
    fill_power_constant(power, 1.0e30f);
    check_successful_case(&plan, &workspace, power, TEST_EPSILON_F);

    /* Tiny input values remain safely below the configured epsilon floor. */
    fill_power_constant(power, FLT_TRUE_MIN);
    CHECK(fin_mfcc_extract_log_mel_power(
        &plan, &workspace, power, TEST_EPSILON_F, log_mel) == FIN_MFCC_OK);
    for (size_t i = 0u; i < FIN_MEL_FILTER_COUNT; ++i) {
        CHECK_NEAR(log_mel[i], silence_log, 1.0e-6f);
    }
}

static void test_status_codes_and_failure_atomicity(void)
{
    FinMfccPlan plan = {0};
    FinMfccPlan uninitialized = {0};
    FinMfccWorkspace workspace = {0};
    FinMfccWorkspace workspace_before;
    float power[FIN_MFCC_FFT_BINS];
    float mfcc_output[FIN_MFCC_COUNT];
    float mfcc_before[FIN_MFCC_COUNT];
    float log_output[FIN_MEL_FILTER_COUNT];
    float log_before[FIN_MEL_FILTER_COUNT];

    CHECK(fin_mfcc_init(&plan, 16000u) == FIN_MFCC_OK);
    fill_power_constant(power, 0.25f);
    for (size_t i = 0u; i < FIN_MFCC_COUNT; ++i) {
        mfcc_output[i] = 123.0f + (float)i;
    }
    for (size_t i = 0u; i < FIN_MEL_FILTER_COUNT; ++i) {
        log_output[i] = -123.0f - (float)i;
    }
    memcpy(mfcc_before, mfcc_output, sizeof(mfcc_before));
    memcpy(log_before, log_output, sizeof(log_before));
    for (size_t i = 0u; i < FIN_MEL_FILTER_COUNT; ++i) {
        workspace.log_mel[i] = 77.0f + (float)i;
    }
    memcpy(&workspace_before, &workspace, sizeof(workspace));

    CHECK(fin_mfcc_extract_power(NULL, &workspace, power, TEST_EPSILON_F,
                                 mfcc_output) == FIN_MFCC_ERROR_INVALID_ARGUMENT);
    CHECK(fin_mfcc_extract_power(&plan, NULL, power, TEST_EPSILON_F,
                                 mfcc_output) == FIN_MFCC_ERROR_INVALID_ARGUMENT);
    CHECK(fin_mfcc_extract_power(&plan, &workspace, NULL, TEST_EPSILON_F,
                                 mfcc_output) == FIN_MFCC_ERROR_INVALID_ARGUMENT);
    CHECK(fin_mfcc_extract_power(&plan, &workspace, power, TEST_EPSILON_F,
                                 NULL) == FIN_MFCC_ERROR_INVALID_ARGUMENT);
    CHECK(fin_mfcc_extract_power(&uninitialized, &workspace, power,
                                 TEST_EPSILON_F, mfcc_output) ==
          FIN_MFCC_ERROR_NOT_INITIALIZED);
    check_output_unchanged(mfcc_output, mfcc_before, FIN_MFCC_COUNT);

    CHECK(fin_mfcc_extract_log_mel_power(NULL, &workspace, power,
          TEST_EPSILON_F, log_output) == FIN_MFCC_ERROR_INVALID_ARGUMENT);
    CHECK(fin_mfcc_extract_log_mel_power(&plan, NULL, power,
          TEST_EPSILON_F, log_output) == FIN_MFCC_ERROR_INVALID_ARGUMENT);
    CHECK(fin_mfcc_extract_log_mel_power(&plan, &workspace, NULL,
          TEST_EPSILON_F, log_output) == FIN_MFCC_ERROR_INVALID_ARGUMENT);
    CHECK(fin_mfcc_extract_log_mel_power(&plan, &workspace, power,
          TEST_EPSILON_F, NULL) == FIN_MFCC_ERROR_INVALID_ARGUMENT);
    CHECK(fin_mfcc_extract_log_mel_power(&uninitialized, &workspace, power,
          TEST_EPSILON_F, log_output) == FIN_MFCC_ERROR_NOT_INITIALIZED);
    check_output_unchanged(log_output, log_before, FIN_MEL_FILTER_COUNT);

    CHECK(fin_mfcc_extract_power(&plan, &workspace, power, 0.0f,
          mfcc_output) == FIN_MFCC_ERROR_INVALID_CONFIG);
    CHECK(fin_mfcc_extract_power(&plan, &workspace, power, -1.0f,
          mfcc_output) == FIN_MFCC_ERROR_INVALID_CONFIG);
    CHECK(fin_mfcc_extract_power(&plan, &workspace, power, NAN,
          mfcc_output) == FIN_MFCC_ERROR_INVALID_CONFIG);
    CHECK(fin_mfcc_extract_power(&plan, &workspace, power, INFINITY,
          mfcc_output) == FIN_MFCC_ERROR_INVALID_CONFIG);
    check_output_unchanged(mfcc_output, mfcc_before, FIN_MFCC_COUNT);

    /* Non-finite and negative spectrum values are numeric failures. */
    power[7] = -0.25f;
    CHECK(fin_mfcc_extract_power(&plan, &workspace, power, TEST_EPSILON_F,
          mfcc_output) == FIN_MFCC_ERROR_NUMERIC_FAILURE);
    CHECK(fin_mfcc_extract_log_mel_power(&plan, &workspace, power,
          TEST_EPSILON_F, log_output) == FIN_MFCC_ERROR_NUMERIC_FAILURE);
    check_output_unchanged(mfcc_output, mfcc_before, FIN_MFCC_COUNT);
    check_output_unchanged(log_output, log_before, FIN_MEL_FILTER_COUNT);
    CHECK(memcmp(&workspace, &workspace_before, sizeof(workspace)) == 0);

    power[7] = NAN;
    CHECK(fin_mfcc_extract_power(&plan, &workspace, power, TEST_EPSILON_F,
          mfcc_output) == FIN_MFCC_ERROR_NUMERIC_FAILURE);
    check_output_unchanged(mfcc_output, mfcc_before, FIN_MFCC_COUNT);
    power[7] = INFINITY;
    CHECK(fin_mfcc_extract_power(&plan, &workspace, power, TEST_EPSILON_F,
          mfcc_output) == FIN_MFCC_ERROR_NUMERIC_FAILURE);
    check_output_unchanged(mfcc_output, mfcc_before, FIN_MFCC_COUNT);

    /* Finite inputs can still overflow during weighted accumulation. Output
     * and workspace must not expose a partially computed result. */
    fill_power_constant(power, FLT_MAX);
    CHECK(fin_mfcc_extract_power(&plan, &workspace, power, TEST_EPSILON_F,
          mfcc_output) == FIN_MFCC_ERROR_NUMERIC_FAILURE);
    check_output_unchanged(mfcc_output, mfcc_before, FIN_MFCC_COUNT);
    CHECK(memcmp(&workspace, &workspace_before, sizeof(workspace)) == 0);
}

static void test_workspace_alias_and_repeatability(void)
{
    FinMfccPlan plan = {0};
    FinMfccWorkspace workspace = {0};
    float power[FIN_MFCC_FFT_BINS];
    float expected[FIN_MEL_FILTER_COUNT];
    float first[FIN_MFCC_COUNT];
    float second[FIN_MFCC_COUNT];

    CHECK(fin_mfcc_init(&plan, 16000u) == FIN_MFCC_OK);
    fill_power_deterministic(power);

    reference_log_mel(&plan, power, TEST_EPSILON_F, expected);
    CHECK(fin_mfcc_extract_log_mel_power(
        &plan, &workspace, power, TEST_EPSILON_F, workspace.log_mel) ==
        FIN_MFCC_OK);
    for (size_t i = 0u; i < FIN_MEL_FILTER_COUNT; ++i) {
        CHECK_NEAR(workspace.log_mel[i], expected[i], TEST_ABS_TOL_F);
    }

    CHECK(fin_mfcc_extract_power(
        &plan, &workspace, power, TEST_EPSILON_F, first) == FIN_MFCC_OK);
    CHECK(fin_mfcc_extract_power(
        &plan, &workspace, power, TEST_EPSILON_F, second) == FIN_MFCC_OK);
    CHECK(floats_equal_exact(first, second, FIN_MFCC_COUNT));
}

int main(void)
{
    (void)printf("MFCC object sizes: plan=%zu bytes, workspace=%zu bytes\n",
                 sizeof(FinMfccPlan), sizeof(FinMfccWorkspace));
    (void)printf("MFCC alignment: plan=%zu, workspace=%zu bytes\n",
                 (size_t)_Alignof(FinMfccPlan),
                 (size_t)_Alignof(FinMfccWorkspace));

    test_alignment_and_initialization();
    test_known_spectra();
    test_status_codes_and_failure_atomicity();
    test_workspace_alias_and_repeatability();

    if (g_failures != 0u) {
        (void)fprintf(stderr, "MFCC tests: %lu/%lu checks failed\n",
                      g_failures, g_checks);
        return EXIT_FAILURE;
    }

    (void)printf("MFCC tests: %lu checks passed\n", g_checks);
    return EXIT_SUCCESS;
}
