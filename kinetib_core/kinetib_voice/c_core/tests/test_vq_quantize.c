/*
 * FinTally Voice — generated-codebook VQ regression tests.
 *
 * Tests the public VQ API against the exporter-generated English, French and
 * Hindi FP32 centroid tables. The generated codebook files are inputs only:
 * this test must never rewrite or regenerate them.
 */

#include "vq_quantize.h"
#include "codebooks/fin_voice_codebook_data.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_RANDOM_QUERIES 128u
#define TEST_TOLERANCE 1.0e-5f

static unsigned g_failures = 0u;
static unsigned g_checks = 0u;

#define CHECK(condition, message)                                             \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(condition)) {                                                  \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
                          (message));                                         \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static int almost_equal_f32(float actual, float expected)
{
    const float scale = fmaxf(1.0f, fmaxf(fabsf(actual), fabsf(expected)));
    return isfinite(actual) && isfinite(expected) &&
           fabsf(actual - expected) <= TEST_TOLERANCE * scale;
}

/*
 * Deliberately exhaustive reference. Unlike the production routine, it does
 * not use partial-distance pruning. It uses FP32 accumulation so it verifies
 * the exact runtime numeric contract while independently checking the top-two
 * selection logic.
 */
static FinVqStatus reference_quantize_f32(
    const float *vector,
    size_t vector_dimension,
    const FinVqCodebook *codebook,
    FinVqResult *result)
{
    if (vector == NULL || codebook == NULL || result == NULL ||
        codebook->centroids == NULL || codebook->centroid_count == 0u ||
        codebook->dimension == 0u) {
        return FIN_VQ_ERROR_INVALID_ARGUMENT;
    }

    if (vector_dimension != codebook->dimension) {
        return FIN_VQ_ERROR_DIMENSION_MISMATCH;
    }

    float best = INFINITY;
    float second = INFINITY;
    size_t best_index = FIN_VQ_NO_INDEX;
    size_t second_index = FIN_VQ_NO_INDEX;

    for (size_t i = 0u; i < codebook->centroid_count; ++i) {
        float distance = 0.0f;
        const float *centroid =
            codebook->centroids + i * codebook->dimension;

        for (size_t d = 0u; d < codebook->dimension; ++d) {
            const float difference = vector[d] - centroid[d];
            const float term = difference * difference;
            if (!isfinite(difference) || !isfinite(term)) {
                distance = INFINITY;
                break;
            }
            distance += term;
            if (!isfinite(distance)) {
                distance = INFINITY;
                break;
            }
        }

        if (distance < best) {
            second = best;
            second_index = best_index;
            best = distance;
            best_index = i;
        } else if (distance < second) {
            second = distance;
            second_index = i;
        }
    }

    if (best_index == FIN_VQ_NO_INDEX || !isfinite(best)) {
        return FIN_VQ_ERROR_NUMERIC_FAILURE;
    }

    result->best_index = best_index;
    result->second_best_index = second_index;
    result->best_distance_squared = best;
    result->second_best_distance_squared =
        (second_index == FIN_VQ_NO_INDEX) ? INFINITY : second;

    if (second_index == FIN_VQ_NO_INDEX) {
        result->relative_gap = 0.0f;
    } else {
        const float denominator = fmaxf(second, FLT_MIN);
        float gap = (second - best) / denominator;
        if (gap < 0.0f) {
            gap = 0.0f;
        } else if (gap > 1.0f) {
            gap = 1.0f;
        }
        result->relative_gap = gap;
    }

    return FIN_VQ_OK;
}

static uint32_t next_random_u32(uint32_t *state)
{
    /* xorshift32; deterministic, allocation-free test vectors. */
    uint32_t x = *state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    *state = x;
    return x;
}

static float random_normalized_feature(uint32_t *state)
{
    /* Top 24 bits produce a stable value in [-8, 8), representable as FP32. */
    const uint32_t mantissa = next_random_u32(state) >> 8u;
    const float unit = (float)mantissa * (1.0f / 16777216.0f);
    return (unit * 16.0f) - 8.0f;
}

static void check_result_matches_reference(
    const float *vector,
    const FinVqCodebook *codebook,
    const char *language,
    const char *case_name)
{
    FinVqResult actual = {0};
    FinVqResult expected = {0};

    const FinVqStatus actual_status = fin_vq_quantize_validated(
        vector, codebook->dimension, codebook, &actual);
    const FinVqStatus expected_status = reference_quantize_f32(
        vector, codebook->dimension, codebook, &expected);

    if (actual_status != FIN_VQ_OK || expected_status != FIN_VQ_OK) {
        (void)fprintf(stderr,
                      "FAIL %s/%s: status actual=%s reference=%s\n",
                      language, case_name,
                      fin_vq_status_name(actual_status),
                      fin_vq_status_name(expected_status));
        CHECK(actual_status == FIN_VQ_OK, "production quantizer status");
        CHECK(expected_status == FIN_VQ_OK, "reference quantizer status");
        return;
    }

    CHECK(actual.best_index == expected.best_index,
          "nearest centroid differs from exhaustive reference");
    CHECK(actual.second_best_index == expected.second_best_index,
          "second-nearest centroid differs from exhaustive reference");
    CHECK(almost_equal_f32(actual.best_distance_squared,
                           expected.best_distance_squared),
          "best squared distance differs from exhaustive reference");
    CHECK(almost_equal_f32(actual.second_best_distance_squared,
                           expected.second_best_distance_squared),
          "second-best squared distance differs from exhaustive reference");
    CHECK(almost_equal_f32(actual.relative_gap, expected.relative_gap),
          "relative gap differs from exhaustive reference");

    CHECK(actual.best_index < codebook->centroid_count,
          "best index is outside the codebook");
    CHECK(actual.second_best_index == FIN_VQ_NO_INDEX ||
              actual.second_best_index < codebook->centroid_count,
          "second-best index is outside the codebook");
    CHECK(isfinite(actual.best_distance_squared) &&
              actual.best_distance_squared >= 0.0f,
          "best squared distance must be finite and non-negative");
    CHECK(isfinite(actual.relative_gap) && actual.relative_gap >= 0.0f &&
              actual.relative_gap <= 1.0f,
          "relative gap must be finite and within [0, 1]");
}

static void test_generated_codebook_contract(void)
{
    static const char *const expected_languages[] = {"en", "fr", "hi"};

    CHECK(FIN_VOICE_FEATURE_DIMENSION == 32u,
          "generated feature dimension must be 32");
    CHECK(FIN_VOICE_CODEBOOK_LANGUAGE_COUNT == 3u,
          "current generated bundle must contain en/fr/hi only");
    CHECK(FIN_VOICE_CODEBOOK_EN_CENTROID_COUNT == 128u,
          "English generated codebook must contain 128 centroids");
    CHECK(FIN_VOICE_CODEBOOK_FR_CENTROID_COUNT == 128u,
          "French generated codebook must contain 128 centroids");
    CHECK(FIN_VOICE_CODEBOOK_HI_CENTROID_COUNT == 128u,
          "Hindi generated codebook must contain 128 centroids");

    for (size_t d = 0u; d < FIN_VOICE_FEATURE_DIMENSION; ++d) {
        CHECK(isfinite(fin_voice_feature_normalization_mean[d]),
              "normalization mean must be finite");
        CHECK(isfinite(fin_voice_feature_normalization_inv_std[d]) &&
                  fin_voice_feature_normalization_inv_std[d] > 0.0f,
              "normalization inverse standard deviation must be finite and positive");
    }

    for (size_t language_index = 0u;
         language_index < FIN_VOICE_CODEBOOK_LANGUAGE_COUNT;
         ++language_index) {
        const FinVoiceCodebookView *view = &fin_voice_codebooks[language_index];

        CHECK(view->language != NULL, "generated language label must not be NULL");
        if (view->language != NULL) {
            CHECK(strcmp(view->language, expected_languages[language_index]) == 0,
                  "generated codebook language ordering must be en/fr/hi");
        }

        CHECK(view->centroids != NULL,
              "generated centroid pointer must not be NULL");
        CHECK(view->dimensions == FIN_VOICE_FEATURE_DIMENSION,
              "generated codebook dimension must match feature dimension");
        CHECK(view->centroid_count == 128u,
              "each supported language must have 128 centroids");
        CHECK(view->training_samples >= view->centroid_count,
              "generated training count must be at least centroid count");
        CHECK(view->converged == 1u,
              "runtime codebook should have converged during training");
        CHECK(view->backend == 1u || view->backend == 2u,
              "generated backend id must identify CPU or CUDA");

        if (view->centroids == NULL) {
            continue;
        }

        const FinVqCodebook codebook = {
            .centroids = view->centroids,
            .centroid_count = (size_t)view->centroid_count,
            .dimension = (size_t)view->dimensions,
        };

        CHECK(fin_vq_validate_codebook(&codebook) == FIN_VQ_OK,
              "generated codebook must pass VQ validation");

        /* Every stored FP32 centroid must be finite. */
        const size_t element_count =
            codebook.centroid_count * codebook.dimension;
        for (size_t element = 0u; element < element_count; ++element) {
            CHECK(isfinite(codebook.centroids[element]),
                  "generated centroid value must be finite");
        }

        /* Quantizing a centroid against its own codebook must yield distance 0.
         * If duplicate centroids exist, deterministic tie-breaking may return
         * an earlier index, which is valid.
         */
        for (size_t centroid_index = 0u;
             centroid_index < codebook.centroid_count;
             ++centroid_index) {
            const float *vector = codebook.centroids +
                                  centroid_index * codebook.dimension;
            FinVqResult result = {0};
            const FinVqStatus status = fin_vq_quantize_validated(
                vector, codebook.dimension, &codebook, &result);

            CHECK(status == FIN_VQ_OK,
                  "an exact generated centroid must quantize successfully");
            if (status == FIN_VQ_OK) {
                CHECK(result.best_distance_squared == 0.0f,
                      "exact centroid input must have zero nearest distance");
                CHECK(result.best_index <= centroid_index,
                      "exact centroid ties must prefer the lowest index");
            }

            check_result_matches_reference(
                vector, &codebook, view->language, "centroid");
        }

        /* Use deterministic probes over a normalized-scale range and compare
         * the pruned implementation with an exhaustive FP32 reference.
         */
        uint32_t random_state =
            UINT32_C(0x91E10DA5) ^ (uint32_t)(language_index + 1u);
        float probe[FIN_VOICE_FEATURE_DIMENSION];
        for (size_t query = 0u; query < TEST_RANDOM_QUERIES; ++query) {
            for (size_t d = 0u; d < FIN_VOICE_FEATURE_DIMENSION; ++d) {
                probe[d] = random_normalized_feature(&random_state);
            }
            check_result_matches_reference(
                probe, &codebook, view->language, "deterministic-random");
        }

        /* Exercise the public safe APIs over the exact generated FP32 data. */
        FinVqResult safe_result = {0};
        CHECK(fin_vq_quantize(
                  codebook.centroids, codebook.dimension,
                  &codebook, &safe_result) == FIN_VQ_OK,
              "safe quantize API must accept generated FP32 codebook");
        CHECK(fin_vq_quantize_features32(
                  codebook.centroids, &codebook, &safe_result) == FIN_VQ_OK,
              "32-D convenience API must accept generated codebook");
    }
}

static void test_ties_and_single_centroid(void)
{
    const float tied_centroids[] = {-1.0f, 1.0f, 2.0f};
    const float zero[] = {0.0f};
    const FinVqCodebook tied_book = {
        .centroids = tied_centroids,
        .centroid_count = 3u,
        .dimension = 1u,
    };
    FinVqResult result = {0};

    CHECK(fin_vq_quantize(zero, 1u, &tied_book, &result) == FIN_VQ_OK,
          "tie case should quantize successfully");
    CHECK(result.best_index == 0u,
          "equal nearest distances must prefer the lowest centroid index");
    CHECK(result.second_best_index == 1u,
          "equal second-best distances must preserve lower index");
    CHECK(result.best_distance_squared == 1.0f,
          "tie case best distance should be 1");
    CHECK(result.second_best_distance_squared == 1.0f,
          "tie case second distance should be 1");
    CHECK(result.relative_gap == 0.0f,
          "equal-distance relative gap should be zero");

    const float one_centroid[] = {3.0f};
    const FinVqCodebook one_book = {
        .centroids = one_centroid,
        .centroid_count = 1u,
        .dimension = 1u,
    };
    const float query[] = {2.0f};

    CHECK(fin_vq_quantize(query, 1u, &one_book, &result) == FIN_VQ_OK,
          "single-centroid codebook should be supported by generic API");
    CHECK(result.best_index == 0u, "single centroid index should be zero");
    CHECK(result.second_best_index == FIN_VQ_NO_INDEX,
          "single centroid has no second-best index");
    CHECK(isinf(result.second_best_distance_squared),
          "single centroid second distance should be infinity");
    CHECK(result.relative_gap == 0.0f,
          "single centroid relative gap should be zero");
}

static void test_invalid_arguments_and_codebooks(void)
{
    FinVqResult result = {0};
    const float valid_centroids[] = {0.0f, 1.0f};
    const float valid_vector[] = {0.25f};
    const FinVqCodebook valid_book = {
        .centroids = valid_centroids,
        .centroid_count = 2u,
        .dimension = 1u,
    };

    CHECK(fin_vq_validate_codebook(NULL) == FIN_VQ_ERROR_INVALID_CODEBOOK,
          "NULL codebook validation should fail");

    const FinVqCodebook null_data = {
        .centroids = NULL, .centroid_count = 2u, .dimension = 1u
    };
    CHECK(fin_vq_validate_codebook(&null_data) == FIN_VQ_ERROR_INVALID_CODEBOOK,
          "NULL centroid data should be rejected");

    const FinVqCodebook zero_count = {
        .centroids = valid_centroids, .centroid_count = 0u, .dimension = 1u
    };
    CHECK(fin_vq_validate_codebook(&zero_count) == FIN_VQ_ERROR_INVALID_CODEBOOK,
          "zero centroid count should be rejected");

    const FinVqCodebook zero_dimension = {
        .centroids = valid_centroids, .centroid_count = 2u, .dimension = 0u
    };
    CHECK(fin_vq_validate_codebook(&zero_dimension) == FIN_VQ_ERROR_INVALID_CODEBOOK,
          "zero dimension should be rejected");

    const float dummy = 0.0f;
    const FinVqCodebook overflow_book = {
        .centroids = &dummy, .centroid_count = SIZE_MAX, .dimension = 2u
    };
    CHECK(fin_vq_validate_codebook(&overflow_book) == FIN_VQ_ERROR_SIZE_OVERFLOW,
          "centroid element-count overflow should be rejected before access");

    CHECK(fin_vq_quantize(NULL, 1u, &valid_book, &result) ==
              FIN_VQ_ERROR_INVALID_ARGUMENT,
          "NULL vector should be rejected");
    CHECK(fin_vq_quantize(valid_vector, 1u, &valid_book, NULL) ==
              FIN_VQ_ERROR_INVALID_ARGUMENT,
          "NULL result should be rejected");
    CHECK(fin_vq_quantize(valid_vector, 1u, NULL, &result) ==
              FIN_VQ_ERROR_INVALID_ARGUMENT,
          "NULL codebook argument should be rejected");
    CHECK(fin_vq_quantize(valid_vector, 2u, &valid_book, &result) ==
              FIN_VQ_ERROR_DIMENSION_MISMATCH,
          "dimension mismatch should be reported");

    const float nan_vector[] = {NAN};
    CHECK(fin_vq_quantize(nan_vector, 1u, &valid_book, &result) ==
              FIN_VQ_ERROR_NUMERIC_FAILURE,
          "NaN feature should be rejected");
    const float inf_vector[] = {INFINITY};
    CHECK(fin_vq_quantize(inf_vector, 1u, &valid_book, &result) ==
              FIN_VQ_ERROR_NUMERIC_FAILURE,
          "infinite feature should be rejected");

    const float nan_centroids[] = {0.0f, NAN};
    const FinVqCodebook nan_book = {
        .centroids = nan_centroids, .centroid_count = 2u, .dimension = 1u
    };
    CHECK(fin_vq_validate_codebook(&nan_book) == FIN_VQ_ERROR_INVALID_CODEBOOK,
          "NaN centroid should be rejected");
    CHECK(fin_vq_quantize(valid_vector, 1u, &nan_book, &result) ==
              FIN_VQ_ERROR_INVALID_CODEBOOK,
          "safe API should reject a codebook containing NaN");

    const float inf_centroids[] = {0.0f, INFINITY};
    const FinVqCodebook inf_book = {
        .centroids = inf_centroids, .centroid_count = 2u, .dimension = 1u
    };
    CHECK(fin_vq_validate_codebook(&inf_book) == FIN_VQ_ERROR_INVALID_CODEBOOK,
          "infinite centroid should be rejected");

    const float far_centroid[] = {-FLT_MAX};
    const FinVqCodebook far_book = {
        .centroids = far_centroid, .centroid_count = 1u, .dimension = 1u
    };
    const float extreme_vector[] = {FLT_MAX};
    CHECK(fin_vq_quantize(extreme_vector, 1u, &far_book, &result) ==
              FIN_VQ_ERROR_NUMERIC_FAILURE,
          "unrepresentable squared distance should be reported as numeric failure");

    CHECK(strcmp(fin_vq_status_name(FIN_VQ_OK), "ok") == 0,
          "status name for success should be stable");
    CHECK(strcmp(fin_vq_status_name(FIN_VQ_ERROR_SIZE_OVERFLOW),
                 "size_overflow") == 0,
          "status name for overflow should be stable");
    CHECK(strcmp(fin_vq_status_name((FinVqStatus)999), "unknown_status") == 0,
          "unknown status should have a safe fallback name");
    CHECK(fin_vq_abi_version() ==
              ((FIN_VQ_ABI_MAJOR << 16u) | FIN_VQ_ABI_MINOR),
          "ABI version encoding should match header constants");
}

int main(void)
{
    (void)printf("FinTally Voice VQ test suite\n");
    (void)printf("Generated codebook bundle: %u languages, %u-D FP32, "
                 "128 centroids/language\n",
                 FIN_VOICE_CODEBOOK_LANGUAGE_COUNT,
                 FIN_VOICE_FEATURE_DIMENSION);

    test_generated_codebook_contract();
    test_ties_and_single_centroid();
    test_invalid_arguments_and_codebooks();

    if (g_failures != 0u) {
        (void)fprintf(stderr, "FAILED: %u of %u checks failed\n",
                      g_failures, g_checks);
        return 1;
    }

    (void)printf("PASS: %u checks; generated codebook tables were read-only inputs\n",
                 g_checks);
    return 0;
}
