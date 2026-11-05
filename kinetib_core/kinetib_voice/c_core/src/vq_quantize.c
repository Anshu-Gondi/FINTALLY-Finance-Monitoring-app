#include "vq_quantize.h"

#include <float.h>
#include <math.h>
#include <stdint.h>

static int fin_vq_finite(float value)
{
    return isfinite(value) ? 1 : 0;
}

static FinVqStatus fin_vq_validate_shape(const FinVqCodebook *codebook)
{
    if (codebook == NULL || codebook->centroids == NULL ||
        codebook->centroid_count == 0u || codebook->dimension == 0u) {
        return FIN_VQ_ERROR_INVALID_CODEBOOK;
    }

    if (codebook->centroid_count > SIZE_MAX / codebook->dimension) {
        return FIN_VQ_ERROR_SIZE_OVERFLOW;
    }

    return FIN_VQ_OK;
}

FinVqStatus fin_vq_validate_codebook(const FinVqCodebook *codebook)
{
    FinVqStatus status = fin_vq_validate_shape(codebook);
    if (status != FIN_VQ_OK) {
        return status;
    }

    const size_t element_count = codebook->centroid_count * codebook->dimension;
    for (size_t i = 0u; i < element_count; ++i) {
        if (!fin_vq_finite(codebook->centroids[i])) {
            return FIN_VQ_ERROR_INVALID_CODEBOOK;
        }
    }

    return FIN_VQ_OK;
}

static FinVqStatus fin_vq_validate_vector(
    const float *vector,
    size_t vector_dimension,
    const FinVqCodebook *codebook,
    FinVqResult *result)
{
    if (vector == NULL || codebook == NULL || result == NULL) {
        return FIN_VQ_ERROR_INVALID_ARGUMENT;
    }

    const FinVqStatus status = fin_vq_validate_shape(codebook);
    if (status != FIN_VQ_OK) {
        return status;
    }

    if (vector_dimension != codebook->dimension) {
        return FIN_VQ_ERROR_DIMENSION_MISMATCH;
    }

    for (size_t d = 0u; d < vector_dimension; ++d) {
        if (!fin_vq_finite(vector[d])) {
            return FIN_VQ_ERROR_NUMERIC_FAILURE;
        }
    }

    return FIN_VQ_OK;
}

FinVqStatus fin_vq_quantize_validated(
    const float *vector,
    size_t vector_dimension,
    const FinVqCodebook *codebook,
    FinVqResult *result)
{
    const FinVqStatus input_status = fin_vq_validate_vector(
        vector, vector_dimension, codebook, result);
    if (input_status != FIN_VQ_OK) {
        return input_status;
    }

    float best_distance = INFINITY;
    float second_distance = INFINITY;
    size_t best_index = FIN_VQ_NO_INDEX;
    size_t second_index = FIN_VQ_NO_INDEX;

    for (size_t centroid_index = 0u;
         centroid_index < codebook->centroid_count;
         ++centroid_index) {
        const float *centroid = codebook->centroids +
                                centroid_index * codebook->dimension;
        float distance = 0.0f;
        int abandon = 0;

        for (size_t d = 0u; d < codebook->dimension; ++d) {
            const float center_value = centroid[d];
            if (!fin_vq_finite(center_value)) {
                return FIN_VQ_ERROR_INVALID_CODEBOOK;
            }

            const float difference = vector[d] - center_value;
            const float term = difference * difference;
            if (!fin_vq_finite(term)) {
                /* A malformed/extreme candidate must not corrupt best-so-far. */
                abandon = 1;
                break;
            }

            distance += term;
            if (!fin_vq_finite(distance)) {
                abandon = 1;
                break;
            }

            /* Non-negative terms: partial sums only increase. We need top two. */
            if (distance >= second_distance) {
                abandon = 1;
                break;
            }
        }

        if (abandon) {
            continue;
        }

        if (distance < best_distance) {
            second_distance = best_distance;
            second_index = best_index;
            best_distance = distance;
            best_index = centroid_index;
        } else if (distance < second_distance) {
            second_distance = distance;
            second_index = centroid_index;
        }
    }

    if (best_index == FIN_VQ_NO_INDEX || !fin_vq_finite(best_distance)) {
        return FIN_VQ_ERROR_NUMERIC_FAILURE;
    }

    result->best_index = best_index;
    result->second_best_index = second_index;
    result->best_distance_squared = best_distance;
    result->second_best_distance_squared =
        (second_index == FIN_VQ_NO_INDEX) ? INFINITY : second_distance;

    if (second_index == FIN_VQ_NO_INDEX) {
        result->relative_gap = 0.0f;
    } else {
        const float denominator = fmaxf(second_distance, FLT_MIN);
        result->relative_gap = (second_distance - best_distance) / denominator;
        if (result->relative_gap < 0.0f) {
            result->relative_gap = 0.0f;
        } else if (result->relative_gap > 1.0f) {
            result->relative_gap = 1.0f;
        }
    }

    return FIN_VQ_OK;
}

FinVqStatus fin_vq_quantize(
    const float *vector,
    size_t vector_dimension,
    const FinVqCodebook *codebook,
    FinVqResult *result)
{
    FinVqStatus status = fin_vq_validate_vector(
        vector, vector_dimension, codebook, result);
    if (status != FIN_VQ_OK) {
        return status;
    }

    status = fin_vq_validate_codebook(codebook);
    if (status != FIN_VQ_OK) {
        return status;
    }

    return fin_vq_quantize_validated(
        vector, vector_dimension, codebook, result);
}

FinVqStatus fin_vq_quantize_features32(
    const float features[32],
    const FinVqCodebook *codebook,
    FinVqResult *result)
{
    return fin_vq_quantize(features, 32u, codebook, result);
}

const char *fin_vq_status_name(FinVqStatus status)
{
    switch (status) {
        case FIN_VQ_OK: return "ok";
        case FIN_VQ_ERROR_INVALID_ARGUMENT: return "invalid_argument";
        case FIN_VQ_ERROR_INVALID_CODEBOOK: return "invalid_codebook";
        case FIN_VQ_ERROR_DIMENSION_MISMATCH: return "dimension_mismatch";
        case FIN_VQ_ERROR_NUMERIC_FAILURE: return "numeric_failure";
        case FIN_VQ_ERROR_SIZE_OVERFLOW: return "size_overflow";
        default: return "unknown_status";
    }
}

uint32_t fin_vq_abi_version(void)
{
    return (FIN_VQ_ABI_MAJOR << 16u) | FIN_VQ_ABI_MINOR;
}
