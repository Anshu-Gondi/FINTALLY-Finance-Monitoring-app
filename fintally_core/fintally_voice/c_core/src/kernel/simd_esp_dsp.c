#include "kernel/simd_dispatch.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/*
 * FinTally Voice ESP-DSP backend.
 *
 * Public data format:
 *   input       : uint8_t quantized vectors
 *   accumulation: int64_t / uint64_t
 *   output      : float (FP32)
 *
 * This source is safe to include in desktop and non-Espressif builds. The
 * ESP-DSP implementation is enabled only when both ESP_PLATFORM and
 * FIN_VOICE_ENABLE_ESP_DSP are defined and the ESP-DSP component is linked.
 * Otherwise this translation unit exports an unavailable backend table.
 *
 * Why signed-byte scratch tiles are used:
 *   ESP-DSP's dot-product primitive consumes INT8, whereas the public input is
 *   UINT8. Subtracting 128 maps every input exactly into [-128,127]. Dot-product
 *   correction identities recover the requested zero-point-centered result; no
 *   saturation or clipping is introduced. Scratch storage is fixed on the stack.
 *
 * Cache policy for future maintainers:
 *   - The nominal centroid/query tile budget is 100 KiB (102,400 bytes), reduced
 *     from 500 KiB to keep candidate blocks bounded. It is a software schedule,
 *     not a hardware cache reservation and not a guarantee of avoiding L2.
 *   - The actual ESP-DSP feature tile remains 256 bytes because it bounds two
 *     signed-byte scratch arrays to 512 bytes total. The budget calculation
 *     reserves 1 KiB for both arrays, row accumulators and call-frame state.
 *     Do not increase the tile without checking embedded task stack limits.
 *   - Centroid processing is feature-tile-major so the same query tile is reused
 *     across a small block of rows. The row cap is four, which keeps the input
 *     portion near 1.25 KiB for a full feature tile; the 1 KiB scratch allowance
 *     still leaves the nominal active set comfortably below the 100 KiB ceiling.
 *     Benchmark on the exact ESP SoC and ESP-DSP version before changing limits.
 *
 * ESP-DSP's array dot-product primitive accepts signed 8-bit vectors. To use
 * it without clipping UINT8 inputs, each byte is shifted by 128 into [-128,127]
 * and the affine correction is applied algebraically. Distances use the exact
 * identity sum((a-b)^2) = sum(x^2) + sum(y^2) - 2*sum(x*y), where
 * x=(a-128) and y=(b-128). All calculations stay integer until final scaling.
 * The two conversion scratch arrays and nearest-search accumulator block use the
 * shared 64-byte alignment helper. This aligns objects but does not control L1/L2.
 */

#define FIN_ESP_DSP_TILE_ELEMENTS       256U
#define FIN_ESP_DSP_CENTROID_BLOCK_MAX  4U
#define FIN_ESP_DSP_CACHE_BUDGET_BYTES  (100U * 1024U)
/* Two INT8 conversion buffers, each FIN_ESP_DSP_TILE_ELEMENTS bytes. */
/* Includes 512 bytes of INT8 conversion arrays plus bounded row accumulators
 * and call-frame state; the nominal budget is intentionally conservative. */
#define FIN_ESP_DSP_SCRATCH_BUDGET_BYTES 1024U
#define FIN_ESP_DSP_MAX_SQUARED_DIFF    UINT64_C(65025)

#if defined(ESP_PLATFORM) && defined(FIN_VOICE_ENABLE_ESP_DSP)

#include "dsps_dotprod.h"

/*
 * ESP-DSP documents dsps_dp_s8_aes3 as the ESP32-S3 optimized implementation.
 * Other targets use its portable ANSI implementation. ESP-DSP configuration
 * and the target chip are determined by ESP-IDF at build time.
 */
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define FIN_ESP_DSP_DOT_S8 dsps_dp_s8_aes3
#define FIN_ESP_DSP_BACKEND_NAME "esp-dsp-s3"
#else
#define FIN_ESP_DSP_DOT_S8 dsps_dp_s8_ansi
#define FIN_ESP_DSP_BACKEND_NAME "esp-dsp-ansi"
#endif

/* ------------------------------------------------------------------------- */
/* Validation and output scaling                                             */
/* ------------------------------------------------------------------------- */

static bool fin_esp_dsp_scale_valid(float scale)
{
    return isfinite(scale) && scale > 0.0f;
}

static bool fin_esp_dsp_valid_pair(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count)
{
    return count == 0U || (lhs != NULL && rhs != NULL);
}

static bool fin_esp_dsp_count_fits_distance(size_t count)
{
    return (uintmax_t)count <=
           ((uintmax_t)UINT64_MAX / FIN_ESP_DSP_MAX_SQUARED_DIFF);
}

static bool fin_esp_dsp_count_fits_dot(size_t count)
{
    return (uintmax_t)count <=
           ((uintmax_t)INT64_MAX / FIN_ESP_DSP_MAX_SQUARED_DIFF);
}

static bool fin_esp_dsp_valid_matrix(
    const uint8_t *query,
    const uint8_t *centroids,
    size_t centroid_count,
    size_t dimension,
    size_t *out_index,
    float *out_distance)
{
    if (query == NULL || centroids == NULL || out_index == NULL ||
        out_distance == NULL || centroid_count == 0U || dimension == 0U) {
        return false;
    }

    if (centroid_count > SIZE_MAX / dimension) {
        return false;
    }

    return fin_esp_dsp_count_fits_distance(dimension);
}

static bool fin_esp_dsp_scale_distance(
    uint64_t integer_distance,
    float scale,
    float *out_f32)
{
    const float scaled = (float)integer_distance * scale * scale;

    if (out_f32 == NULL || !isfinite(scaled)) {
        return false;
    }

    *out_f32 = scaled;
    return true;
}

static bool fin_esp_dsp_scale_dot(
    int64_t integer_dot,
    float scale,
    float *out_f32)
{
    const float scaled = (float)integer_dot * scale * scale;

    if (out_f32 == NULL || !isfinite(scaled)) {
        return false;
    }

    *out_f32 = scaled;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Integer tile primitives                                                   */
/* ------------------------------------------------------------------------- */

/*
 * Convert a UINT8 tile to the signed range [-128,127] by subtracting 128.
 * The conversion is exact: the computed int32_t value is always representable
 * as int8_t, so no saturation or implementation-defined narrowing occurs.
 */
static void fin_esp_dsp_center_u8_tile(
    const uint8_t *source,
    int8_t *destination,
    size_t count,
    int64_t *sum_centered,
    uint64_t *sum_squares)
{
    int64_t signed_sum = INT64_C(0);
    uint64_t square_sum = UINT64_C(0);
    size_t i;

    for (i = 0U; i < count; ++i) {
        const int32_t centered = (int32_t)source[i] - INT32_C(128);
        const int32_t square = centered * centered;

        destination[i] = (int8_t)centered;
        signed_sum += (int64_t)centered;
        square_sum += (uint64_t)square;
    }

    *sum_centered = signed_sum;
    *sum_squares = square_sum;
}

/*
 * Compute sum(lhs[i] * rhs[i]) for two signed-byte tiles using ESP-DSP.
 * The ESP-DSP API accumulates into *destination, so initialize it to zero.
 * Tile length always fits its int length parameter.
 */
static bool fin_esp_dsp_signed_dot_tile(
    const int8_t *lhs,
    const int8_t *rhs,
    size_t count,
    int64_t *out_dot)
{
    int32_t tile_dot = 0;

    if (out_dot == NULL || count > (size_t)INT32_MAX) {
        return false;
    }

    if (count == 0U) {
        *out_dot = INT64_C(0);
        return true;
    }

    if (lhs == NULL || rhs == NULL) {
        return false;
    }

    if (FIN_ESP_DSP_DOT_S8(lhs, rhs, &tile_dot, (int)count) != ESP_OK) {
        return false;
    }

    *out_dot = (int64_t)tile_dot;
    return true;
}

/* ------------------------------------------------------------------------- */
/* UINT8 dot product -> FP32                                                 */
/* ------------------------------------------------------------------------- */

static bool fin_esp_dsp_dot_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    uint8_t zero_point,
    float *out_f32)
{
    FIN_VOICE_CACHE_ALIGN int8_t lhs_tile[FIN_ESP_DSP_TILE_ELEMENTS];
    FIN_VOICE_CACHE_ALIGN int8_t rhs_tile[FIN_ESP_DSP_TILE_ELEMENTS];
    size_t start = 0U;
    int64_t total = INT64_C(0);
    const int64_t correction = INT64_C(128) - (int64_t)zero_point;

    if (out_f32 == NULL || !fin_esp_dsp_scale_valid(scale) ||
        !fin_esp_dsp_valid_pair(lhs, rhs, count) ||
        !fin_esp_dsp_count_fits_dot(count)) {
        return false;
    }

    if (count == 0U) {
        *out_f32 = 0.0f;
        return true;
    }

    while (start < count) {
        const size_t remaining = count - start;
        const size_t tile_count =
            remaining < (size_t)FIN_ESP_DSP_TILE_ELEMENTS
                ? remaining
                : (size_t)FIN_ESP_DSP_TILE_ELEMENTS;
        int64_t lhs_sum = INT64_C(0);
        int64_t rhs_sum = INT64_C(0);
        uint64_t unused_lhs_squares = UINT64_C(0);
        uint64_t unused_rhs_squares = UINT64_C(0);
        int64_t centered_dot = INT64_C(0);
        int64_t corrected_dot;

        fin_esp_dsp_center_u8_tile(
            lhs + start, lhs_tile, tile_count,
            &lhs_sum, &unused_lhs_squares);
        fin_esp_dsp_center_u8_tile(
            rhs + start, rhs_tile, tile_count,
            &rhs_sum, &unused_rhs_squares);

        if (!fin_esp_dsp_signed_dot_tile(
                lhs_tile, rhs_tile, tile_count, &centered_dot)) {
            return false;
        }

        /*
         * For x=a-128 and y=b-128:
         * (a-z)(b-z) = xy + c*x + c*y + c*c,
         * where c=128-z. Apply the exact affine correction in int64.
         */
        corrected_dot = centered_dot +
                        correction * lhs_sum +
                        correction * rhs_sum +
                        (int64_t)tile_count * correction * correction;

        total += corrected_dot;
        start += tile_count;
    }

    return fin_esp_dsp_scale_dot(total, scale, out_f32);
}

/* ------------------------------------------------------------------------- */
/* UINT8 squared distance -> FP32                                            */
/* ------------------------------------------------------------------------- */

static bool fin_esp_dsp_distance_integer_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    uint64_t *out_integer_distance)
{
    FIN_VOICE_CACHE_ALIGN int8_t lhs_tile[FIN_ESP_DSP_TILE_ELEMENTS];
    FIN_VOICE_CACHE_ALIGN int8_t rhs_tile[FIN_ESP_DSP_TILE_ELEMENTS];
    size_t start = 0U;
    uint64_t total = UINT64_C(0);

    if (out_integer_distance == NULL ||
        !fin_esp_dsp_valid_pair(lhs, rhs, count) ||
        !fin_esp_dsp_count_fits_distance(count)) {
        return false;
    }

    while (start < count) {
        const size_t remaining = count - start;
        const size_t tile_count =
            remaining < (size_t)FIN_ESP_DSP_TILE_ELEMENTS
                ? remaining
                : (size_t)FIN_ESP_DSP_TILE_ELEMENTS;
        int64_t lhs_sum = INT64_C(0);
        int64_t rhs_sum = INT64_C(0);
        uint64_t lhs_squares = UINT64_C(0);
        uint64_t rhs_squares = UINT64_C(0);
        int64_t cross_dot = INT64_C(0);
        int64_t tile_distance_signed;

        fin_esp_dsp_center_u8_tile(
            lhs + start, lhs_tile, tile_count,
            &lhs_sum, &lhs_squares);
        fin_esp_dsp_center_u8_tile(
            rhs + start, rhs_tile, tile_count,
            &rhs_sum, &rhs_squares);

        (void)lhs_sum;
        (void)rhs_sum;

        if (!fin_esp_dsp_signed_dot_tile(
                lhs_tile, rhs_tile, tile_count, &cross_dot)) {
            return false;
        }

        /* Each tile is at most 256 * 65025, so signed arithmetic is safe. */
        tile_distance_signed = (int64_t)lhs_squares +
                               (int64_t)rhs_squares -
                               (INT64_C(2) * cross_dot);

        if (tile_distance_signed < 0) {
            /* Defensive check: exact integer identity must never be negative. */
            return false;
        }

        total += (uint64_t)tile_distance_signed;
        start += tile_count;
    }

    *out_integer_distance = total;
    return true;
}

static bool fin_esp_dsp_squared_distance_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    float *out_f32)
{
    uint64_t integer_distance = UINT64_C(0);

    if (out_f32 == NULL || !fin_esp_dsp_scale_valid(scale)) {
        return false;
    }

    if (count == 0U) {
        *out_f32 = 0.0f;
        return true;
    }

    if (!fin_esp_dsp_distance_integer_u8(
            lhs, rhs, count, &integer_distance)) {
        return false;
    }

    return fin_esp_dsp_scale_distance(integer_distance, scale, out_f32);
}

/* ------------------------------------------------------------------------- */
/* UINT8 nearest-centroid search -> FP32 distance                             */
/* ------------------------------------------------------------------------- */

/*
 * Size a centroid block from the streamed tile width, not the full feature
 * row. One query tile + N centroid tiles + the fixed 512-byte conversion
 * scratch allowance stay within the nominal 100 KiB budget whenever possible.
 * The block maximum also keeps accumulator state bounded on ESP-class devices.
 */
static size_t fin_esp_dsp_centroid_block_count(size_t dimension)
{
    size_t tile_bytes;
    size_t available;
    size_t row_count;

    if (dimension == 0U) {
        return 1U;
    }

    tile_bytes = dimension < (size_t)FIN_ESP_DSP_TILE_ELEMENTS
        ? dimension : (size_t)FIN_ESP_DSP_TILE_ELEMENTS;

    if (tile_bytes + (size_t)FIN_ESP_DSP_SCRATCH_BUDGET_BYTES >=
        (size_t)FIN_ESP_DSP_CACHE_BUDGET_BYTES) {
        return 1U;
    }

    available = (size_t)FIN_ESP_DSP_CACHE_BUDGET_BYTES - tile_bytes -
                (size_t)FIN_ESP_DSP_SCRATCH_BUDGET_BYTES;
    row_count = available / tile_bytes;

    if (row_count == 0U) {
        return 1U;
    }
    if (row_count > (size_t)FIN_ESP_DSP_CENTROID_BLOCK_MAX) {
        row_count = (size_t)FIN_ESP_DSP_CENTROID_BLOCK_MAX;
    }

    return row_count;
}

static bool fin_esp_dsp_nearest_u8(
    const uint8_t *query,
    const uint8_t *centroids,
    size_t centroid_count,
    size_t dimension,
    float scale,
    size_t *out_index,
    float *out_squared_distance_f32)
{
    size_t block_start = 0U;
    size_t best_index = 0U;
    uint64_t best_distance = UINT64_MAX;
    bool have_best = false;
    const size_t block_capacity =
        fin_esp_dsp_centroid_block_count(dimension);

    if (!fin_esp_dsp_scale_valid(scale) ||
        !fin_esp_dsp_valid_matrix(
            query, centroids, centroid_count, dimension,
            out_index, out_squared_distance_f32)) {
        return false;
    }

    while (block_start < centroid_count) {
        const size_t remaining = centroid_count - block_start;
        const size_t block_count = remaining < block_capacity
                                       ? remaining
                                       : block_capacity;
        FIN_VOICE_CACHE_ALIGN uint64_t
            block_distances[FIN_ESP_DSP_CENTROID_BLOCK_MAX] = { UINT64_C(0) };
        size_t feature_offset = 0U;
        size_t local_row;

        /*
         * Iterate feature tiles outside rows so a query tile is reused across
         * adjacent centroids. Every sub-call is <=256 elements, preserving the
         * fixed 512-byte INT8 scratch bound in fin_esp_dsp_distance_integer_u8.
         */
        while (feature_offset < dimension) {
            const size_t remaining_features = dimension - feature_offset;
            const size_t tile_count =
                remaining_features < (size_t)FIN_ESP_DSP_TILE_ELEMENTS
                    ? remaining_features
                    : (size_t)FIN_ESP_DSP_TILE_ELEMENTS;
            const uint8_t *const query_tile = query + feature_offset;

            for (local_row = 0U; local_row < block_count; ++local_row) {
                const size_t row = block_start + local_row;
                const uint8_t *const centroid_tile =
                    centroids + row * dimension + feature_offset;
                uint64_t tile_distance = UINT64_C(0);

                if (!fin_esp_dsp_distance_integer_u8(
                        query_tile, centroid_tile, tile_count, &tile_distance)) {
                    return false;
                }
                block_distances[local_row] += tile_distance;
            }

            feature_offset += tile_count;
        }

        for (local_row = 0U; local_row < block_count; ++local_row) {
            const size_t row = block_start + local_row;
            const uint64_t distance = block_distances[local_row];

            /* Keep the lower row on ties for deterministic codebook IDs. */
            if (!have_best || distance < best_distance) {
                best_distance = distance;
                best_index = row;
                have_best = true;
            }
        }

        block_start += block_count;
    }

    if (!have_best ||
        !fin_esp_dsp_scale_distance(
            best_distance, scale, out_squared_distance_f32)) {
        return false;
    }

    *out_index = best_index;
    return true;
}

static bool fin_voice_simd_esp_dsp_is_supported(void)
{
    /* This branch is compiled only for an explicitly configured ESP-DSP build. */
    return true;
}

const FinVoiceSimdOps fin_voice_simd_esp_dsp_ops = {
    .backend = FIN_VOICE_SIMD_ESP_DSP,
    .name = FIN_ESP_DSP_BACKEND_NAME,
    .is_supported = fin_voice_simd_esp_dsp_is_supported,
    .dot_u8 = fin_esp_dsp_dot_u8,
    .squared_distance_u8 = fin_esp_dsp_squared_distance_u8,
    .nearest_u8 = fin_esp_dsp_nearest_u8
};

#else /* not an explicitly configured ESP-DSP build */

static bool fin_voice_simd_esp_dsp_is_supported(void)
{
    return false;
}

const FinVoiceSimdOps fin_voice_simd_esp_dsp_ops = {
    .backend = FIN_VOICE_SIMD_ESP_DSP,
    .name = "esp-dsp-unavailable",
    .is_supported = fin_voice_simd_esp_dsp_is_supported,
    .dot_u8 = NULL,
    .squared_distance_u8 = NULL,
    .nearest_u8 = NULL
};

#endif /* ESP_PLATFORM && FIN_VOICE_ENABLE_ESP_DSP */
