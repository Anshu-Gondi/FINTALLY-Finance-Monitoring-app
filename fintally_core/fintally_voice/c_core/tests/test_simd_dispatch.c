#include "kernel/simd_dispatch.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_MAX_DIMENSION 4097U
#define TEST_MAX_CENTROIDS 19U
#define TEST_SCALE_REL_TOLERANCE 2.0e-5f

static size_t g_checks;
static size_t g_failures;
static uint32_t g_rng_state = UINT32_C(0xC001D00D);

/* Offset test buffers by one byte to exercise ordinary unaligned caller data. */
static uint8_t g_lhs_storage[TEST_MAX_DIMENSION + 2U];
static uint8_t g_rhs_storage[TEST_MAX_DIMENSION + 2U];
static uint8_t g_query_storage[TEST_MAX_DIMENSION + 2U];
static uint8_t g_centroid_storage[(TEST_MAX_DIMENSION * TEST_MAX_CENTROIDS) + 2U];

#define CHECK(condition) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            ++g_failures; \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n", \
                          __FILE__, __LINE__, #condition); \
        } \
    } while (0)

static bool test_float_near(float actual, float expected)
{
    float difference = actual - expected;
    float magnitude = expected;
    float tolerance;

    if (!isfinite(actual) || !isfinite(expected)) {
        return false;
    }
    if (difference < 0.0f) {
        difference = -difference;
    }
    if (magnitude < 0.0f) {
        magnitude = -magnitude;
    }
    if (magnitude < 1.0f) {
        magnitude = 1.0f;
    }
    tolerance = TEST_SCALE_REL_TOLERANCE * magnitude;
    return difference <= tolerance;
}

static uint32_t test_random_u32(void)
{
    /* Fixed-seed xorshift32 makes failures reproducible between runs. */
    uint32_t x = g_rng_state;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    g_rng_state = x;
    return x;
}

static void test_fill_random(uint8_t *buffer, size_t count)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        buffer[i] = (uint8_t)(test_random_u32() & UINT32_C(0xFF));
    }
}

static float test_reference_dot(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    uint8_t zero_point)
{
    int64_t total = INT64_C(0);
    size_t i;

    for (i = 0U; i < count; ++i) {
        const int32_t a = (int32_t)lhs[i] - (int32_t)zero_point;
        const int32_t b = (int32_t)rhs[i] - (int32_t)zero_point;
        total += (int64_t)(a * b);
    }
    return (float)total * scale * scale;
}

static float test_reference_squared_distance(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale)
{
    uint64_t total = UINT64_C(0);
    size_t i;

    for (i = 0U; i < count; ++i) {
        const int32_t difference = (int32_t)lhs[i] - (int32_t)rhs[i];
        total += (uint64_t)(difference * difference);
    }
    return (float)total * scale * scale;
}

static bool test_reference_nearest(
    const uint8_t *query,
    const uint8_t *centroids,
    size_t centroid_count,
    size_t dimension,
    float scale,
    size_t *out_index,
    float *out_distance)
{
    size_t centroid;
    uint64_t best_distance = UINT64_MAX;
    size_t best_index = 0U;

    if (query == NULL || centroids == NULL || out_index == NULL ||
        out_distance == NULL || centroid_count == 0U || dimension == 0U ||
        centroid_count > SIZE_MAX / dimension) {
        return false;
    }

    for (centroid = 0U; centroid < centroid_count; ++centroid) {
        const uint8_t *const row = centroids + (centroid * dimension);
        uint64_t distance = UINT64_C(0);
        size_t i;

        for (i = 0U; i < dimension; ++i) {
            const int32_t difference = (int32_t)query[i] - (int32_t)row[i];
            distance += (uint64_t)(difference * difference);
        }

        /* Strict comparison deliberately preserves the first index on ties. */
        if (centroid == 0U || distance < best_distance) {
            best_distance = distance;
            best_index = centroid;
        }
    }

    *out_index = best_index;
    *out_distance = (float)best_distance * scale * scale;
    return isfinite(*out_distance);
}

static void test_known_vectors(const FinVoiceSimdContext *context)
{
    static const uint8_t lhs[] = { 128U, 129U, 127U, 255U, 0U };
    static const uint8_t rhs[] = { 255U, 1U, 128U, 1U, 200U };
    static const uint8_t query[] = { 10U, 10U, 10U, 10U };
    static const uint8_t centroids[] = {
        11U, 10U, 10U, 10U,
         9U, 10U, 10U, 10U,
       100U,100U,100U,100U
    };
    float actual = 0.0f;
    size_t actual_index = SIZE_MAX;
    float actual_distance = -1.0f;

    CHECK(fin_voice_simd_dot_u8(context, lhs, rhs, sizeof lhs,
                                0.5f, 128U, &actual));
    CHECK(test_float_near(actual, -6368.0f));

    CHECK(fin_voice_simd_squared_distance_u8(context, lhs, rhs, sizeof lhs,
                                             0.5f, &actual));
    CHECK(test_float_near(actual, 34257.5f));

    /* Centroids 0 and 1 are equally close; the lower index must win. */
    CHECK(fin_voice_simd_nearest_u8(context, query, centroids, 3U, 4U,
                                    0.25f, &actual_index, &actual_distance));
    CHECK(actual_index == 0U);
    CHECK(test_float_near(actual_distance, 0.0625f));
}

static void test_zero_length(const FinVoiceSimdContext *context)
{
    float result = -99.0f;

    CHECK(fin_voice_simd_dot_u8(context, NULL, NULL, 0U,
                                1.0f, 128U, &result));
    CHECK(result == 0.0f);
    result = -99.0f;
    CHECK(fin_voice_simd_squared_distance_u8(context, NULL, NULL, 0U,
                                             1.0f, &result));
    CHECK(result == 0.0f);
}

static void test_invalid_inputs(const FinVoiceSimdContext *context)
{
    static const uint8_t values[] = { 1U, 2U, 3U, 4U };
    float result = 123.0f;
    size_t index = 123U;

    CHECK(!fin_voice_simd_dot_u8(context, NULL, values, 1U,
                                 1.0f, 0U, &result));
    CHECK(!fin_voice_simd_dot_u8(context, values, values, 1U,
                                 0.0f, 0U, &result));
    CHECK(!fin_voice_simd_dot_u8(context, values, values, 1U,
                                 -1.0f, 0U, &result));
    CHECK(!fin_voice_simd_dot_u8(context, values, values, 1U,
                                 NAN, 0U, &result));
    CHECK(!fin_voice_simd_dot_u8(context, values, values, 1U,
                                 INFINITY, 0U, &result));
    CHECK(!fin_voice_simd_dot_u8(context, values, values, 1U,
                                 1.0f, 0U, NULL));

    CHECK(!fin_voice_simd_squared_distance_u8(context, NULL, values, 1U,
                                              1.0f, &result));
    CHECK(!fin_voice_simd_squared_distance_u8(context, values, values, 1U,
                                              0.0f, &result));
    CHECK(!fin_voice_simd_squared_distance_u8(context, values, values, 1U,
                                              NAN, &result));
    CHECK(!fin_voice_simd_squared_distance_u8(context, values, values, 1U,
                                              1.0f, NULL));

    CHECK(!fin_voice_simd_nearest_u8(context, values, values, 0U, 1U,
                                     1.0f, &index, &result));
    CHECK(!fin_voice_simd_nearest_u8(context, values, values, 1U, 0U,
                                     1.0f, &index, &result));
    CHECK(!fin_voice_simd_nearest_u8(context, NULL, values, 1U, 1U,
                                     1.0f, &index, &result));
    CHECK(!fin_voice_simd_nearest_u8(context, values, values, 1U, 1U,
                                     -1.0f, &index, &result));
    CHECK(!fin_voice_simd_nearest_u8(context, values, values, 1U, 1U,
                                     1.0f, NULL, &result));
    CHECK(!fin_voice_simd_nearest_u8(context, values, values, 1U, 1U,
                                     1.0f, &index, NULL));
}

static void test_randomized_equivalence(const FinVoiceSimdContext *context)
{
    static const size_t lengths[] = {
        0U, 1U, 2U, 3U, 4U, 7U, 8U, 15U, 16U, 17U,
        31U, 32U, 33U, 63U, 64U, 65U, 127U, 128U, 129U,
        255U, 256U, 257U, 511U, 512U, 513U, 1023U,
        1024U, 1025U, 4095U, 4096U, 4097U
    };
    uint8_t *const lhs = g_lhs_storage + 1U;
    uint8_t *const rhs = g_rhs_storage + 1U;
    uint8_t *const query = g_query_storage + 1U;
    uint8_t *const centroids = g_centroid_storage + 1U;
    const float scale = 0.03125f;
    const uint8_t zero_point = 128U;
    size_t length_index;

    for (length_index = 0U;
         length_index < sizeof lengths / sizeof lengths[0];
         ++length_index) {
        const size_t dimension = lengths[length_index] == 0U
            ? 1U : lengths[length_index];
        const size_t centroid_count = 13U;
        size_t expected_index = SIZE_MAX;
        size_t actual_index = SIZE_MAX;
        float expected_dot;
        float expected_distance;
        float expected_nearest_distance = 0.0f;
        float actual_dot = 0.0f;
        float actual_distance = 0.0f;
        float actual_nearest_distance = 0.0f;
        size_t row;

        test_fill_random(lhs, lengths[length_index]);
        test_fill_random(rhs, lengths[length_index]);
        test_fill_random(query, dimension);
        for (row = 0U; row < centroid_count; ++row) {
            test_fill_random(centroids + (row * dimension), dimension);
        }

        expected_dot = test_reference_dot(
            lhs, rhs, lengths[length_index], scale, zero_point);
        expected_distance = test_reference_squared_distance(
            lhs, rhs, lengths[length_index], scale);
        CHECK(test_reference_nearest(query, centroids, centroid_count,
                                     dimension, scale, &expected_index,
                                     &expected_nearest_distance));

        CHECK(fin_voice_simd_dot_u8(context, lhs, rhs,
                                    lengths[length_index], scale,
                                    zero_point, &actual_dot));
        CHECK(test_float_near(actual_dot, expected_dot));

        CHECK(fin_voice_simd_squared_distance_u8(
            context, lhs, rhs, lengths[length_index], scale,
            &actual_distance));
        CHECK(test_float_near(actual_distance, expected_distance));

        CHECK(fin_voice_simd_nearest_u8(
            context, query, centroids, centroid_count, dimension, scale,
            &actual_index, &actual_nearest_distance));
        CHECK(actual_index == expected_index);
        CHECK(test_float_near(actual_nearest_distance,
                              expected_nearest_distance));
    }
}

static void test_backend(FinVoiceSimdBackend backend)
{
    FinVoiceSimdContext context = { 0 };
    const char *const name = fin_voice_simd_backend_name(backend);

    if (!fin_voice_simd_backend_available(backend)) {
        (void)printf("SKIP backend %-10s (not linked or not supported)\n", name);
        CHECK(!fin_voice_simd_init(&context, backend));
        CHECK(fin_voice_simd_context_backend(&context) == FIN_SIMD_SCALAR);
        return;
    }

    CHECK(fin_voice_simd_init(&context, backend));
    CHECK(fin_voice_simd_context_backend(&context) == backend);
    (void)printf("TEST backend %-10s\n", name);

    test_known_vectors(&context);
    test_zero_length(&context);
    test_invalid_inputs(&context);
    test_randomized_equivalence(&context);
}

static void test_dispatch_contract(void)
{
    FinVoiceSimdContext context = { 0 };
    const FinVoiceSimdBackend candidates[] = {
        FIN_SIMD_SCALAR,
        FIN_SIMD_SSE42,
        FIN_SIMD_AVX2,
        FIN_SIMD_AVX2_FMA,
        FIN_SIMD_AVX2_VNNI,
        FIN_SIMD_AVX512,
        FIN_SIMD_NEON,
        FIN_SIMD_ESP_DSP
    };
    size_t i;

    CHECK(fin_voice_simd_backend_available(FIN_SIMD_AUTO));
    CHECK(fin_voice_simd_backend_available(FIN_SIMD_SCALAR));
    CHECK(!fin_voice_simd_init(NULL, FIN_SIMD_AUTO));
    CHECK(fin_voice_simd_context_backend(NULL) == FIN_SIMD_AUTO);
    CHECK(!fin_voice_simd_dot_u8(NULL, NULL, NULL, 0U, 1.0f, 0U, NULL));
    CHECK(!fin_voice_simd_squared_distance_u8(NULL, NULL, NULL, 0U,
                                              1.0f, NULL));
    CHECK(!fin_voice_simd_nearest_u8(NULL, NULL, NULL, 0U, 0U, 1.0f,
                                     NULL, NULL));
    CHECK(fin_voice_simd_backend_name((FinVoiceSimdBackend)999) != NULL);

    for (i = 0U; i < sizeof candidates / sizeof candidates[0]; ++i) {
        test_backend(candidates[i]);
    }

    CHECK(!fin_voice_simd_init(&context, (FinVoiceSimdBackend)999));
    CHECK(fin_voice_simd_context_backend(&context) == FIN_SIMD_SCALAR);

    CHECK(fin_voice_simd_init(&context, FIN_SIMD_AUTO));
    CHECK(fin_voice_simd_context_backend(&context) != FIN_SIMD_AUTO);
    CHECK(fin_voice_simd_backend_available(
        fin_voice_simd_context_backend(&context)));
    (void)printf("AUTO selected: %s\n",
                 fin_voice_simd_backend_name(
                     fin_voice_simd_context_backend(&context)));
}

int main(void)
{
    test_dispatch_contract();

    if (g_failures != 0U) {
        (void)fprintf(stderr, "SIMD tests: %zu/%zu checks failed\n",
                      g_failures, g_checks);
        return EXIT_FAILURE;
    }

    (void)printf("SIMD tests passed: %zu checks\n", g_checks);
    return EXIT_SUCCESS;
}
