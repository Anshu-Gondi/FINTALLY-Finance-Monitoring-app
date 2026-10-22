#include "kernel/simd_dispatch.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* Always-available scalar correctness baseline; UINT8 input, FP32 output. */
enum {
    FIN_VOICE_SCALAR_FEATURE_TILE = 256,
    FIN_VOICE_SCALAR_CENTROID_TILE = 16
};

#define FIN_VOICE_U8_MAX_SQ UINT64_C(65025)

static bool fin_scalar_scale_valid(float scale)
{
    return isfinite(scale) && scale > 0.0f;
}

static bool fin_scalar_pair_valid(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float *out
)
{
    if (out == NULL) {
        return false;
    }
    if (count != 0U && (lhs == NULL || rhs == NULL)) {
        return false;
    }
    return true;
}

static bool fin_scalar_dot_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    uint8_t zero_point,
    float *out_f32
)
{
    size_t start;
    int64_t total = INT64_C(0);

    if (!fin_scalar_scale_valid(scale) ||
        !fin_scalar_pair_valid(lhs, rhs, count, out_f32) ||
        (uintmax_t)count > (uintmax_t)(INT64_MAX / INT64_C(65025))) {
        return false;
    }

    for (start = 0U; start < count;) {
        const size_t remain = count - start;
        const size_t tile = remain < (size_t)FIN_VOICE_SCALAR_FEATURE_TILE
            ? remain : (size_t)FIN_VOICE_SCALAR_FEATURE_TILE;
        const size_t end = start + tile;
        int64_t tile_sum = INT64_C(0);
        size_t i;

        for (i = start; i < end; ++i) {
            const int32_t a = (int32_t)lhs[i] - (int32_t)zero_point;
            const int32_t b = (int32_t)rhs[i] - (int32_t)zero_point;
            tile_sum += (int64_t)(a * b);
        }
        total += tile_sum;
        start = end;
    }

    {
        const float result = (float)total * scale * scale;
        if (!isfinite(result)) {
            return false;
        }
        *out_f32 = result;
    }
    return true;
}

static bool fin_scalar_squared_distance_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    float *out_f32
)
{
    size_t start;
    uint64_t total = UINT64_C(0);

    if (!fin_scalar_scale_valid(scale) ||
        !fin_scalar_pair_valid(lhs, rhs, count, out_f32) ||
        count > UINT64_MAX / FIN_VOICE_U8_MAX_SQ) {
        return false;
    }

    for (start = 0U; start < count;) {
        const size_t remain = count - start;
        const size_t tile = remain < (size_t)FIN_VOICE_SCALAR_FEATURE_TILE
            ? remain : (size_t)FIN_VOICE_SCALAR_FEATURE_TILE;
        const size_t end = start + tile;
        uint64_t tile_sum = UINT64_C(0);
        size_t i;

        for (i = start; i < end; ++i) {
            const int32_t diff = (int32_t)lhs[i] - (int32_t)rhs[i];
            tile_sum += (uint64_t)(diff * diff);
        }
        total += tile_sum;
        start = end;
    }

    {
        const float result = (float)total * scale * scale;
        if (!isfinite(result)) {
            return false;
        }
        *out_f32 = result;
    }
    return true;
}

static bool fin_scalar_nearest_u8(
    const uint8_t *query,
    const uint8_t *centroids,
    size_t centroid_count,
    size_t dimension,
    float scale,
    size_t *out_index,
    float *out_distance_f32
)
{
    size_t block_start;
    size_t best_index = 0U;
    uint64_t best_distance = UINT64_MAX;
    bool have_best = false;

    if (query == NULL || centroids == NULL || out_index == NULL ||
        out_distance_f32 == NULL || centroid_count == 0U || dimension == 0U ||
        centroid_count > SIZE_MAX / dimension ||
        dimension > UINT64_MAX / FIN_VOICE_U8_MAX_SQ ||
        !fin_scalar_scale_valid(scale)) {
        return false;
    }

    for (block_start = 0U; block_start < centroid_count;) {
        const size_t remain = centroid_count - block_start;
        const size_t block = remain < (size_t)FIN_VOICE_SCALAR_CENTROID_TILE
            ? remain : (size_t)FIN_VOICE_SCALAR_CENTROID_TILE;
        const size_t block_end = block_start + block;
        size_t centroid_index;

        for (centroid_index = block_start;
             centroid_index < block_end; ++centroid_index) {
            const uint8_t *const row = centroids + centroid_index * dimension;
            size_t feature_start;
            uint64_t distance = UINT64_C(0);

            for (feature_start = 0U; feature_start < dimension;) {
                const size_t feature_remain = dimension - feature_start;
                const size_t tile =
                    feature_remain < (size_t)FIN_VOICE_SCALAR_FEATURE_TILE
                        ? feature_remain
                        : (size_t)FIN_VOICE_SCALAR_FEATURE_TILE;
                const size_t feature_end = feature_start + tile;
                uint64_t tile_sum = UINT64_C(0);
                size_t i;

                for (i = feature_start; i < feature_end; ++i) {
                    const int32_t diff =
                        (int32_t)query[i] - (int32_t)row[i];
                    tile_sum += (uint64_t)(diff * diff);
                }
                distance += tile_sum;
                feature_start = feature_end;
            }

            if (!have_best || distance < best_distance) {
                best_distance = distance;
                best_index = centroid_index;
                have_best = true;
            }
        }
        block_start = block_end;
    }

    if (!have_best) {
        return false;
    }

    {
        const float result = (float)best_distance * scale * scale;
        if (!isfinite(result)) {
            return false;
        }
        *out_index = best_index;
        *out_distance_f32 = result;
    }
    return true;
}

static bool fin_scalar_supported(void)
{
    return true;
}

const FinVoiceSimdOps fin_voice_simd_scalar_ops = {
    .backend = FIN_SIMD_SCALAR,
    .name = "scalar",
    .is_supported = fin_scalar_supported,
    .dot_u8 = fin_scalar_dot_u8,
    .squared_distance_u8 = fin_scalar_squared_distance_u8,
    .nearest_u8 = fin_scalar_nearest_u8
};
