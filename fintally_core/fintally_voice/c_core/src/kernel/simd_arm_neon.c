#include "kernel/simd_dispatch.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/*
 * FinTally Voice ARM NEON backend.
 *
 * Input representation : uint8_t quantized vectors.
 * Internal arithmetic  : integer SIMD; signed dot products use int64_t
 *                        accumulation and distances use uint64_t.
 * Public output        : float (FP32), scaled only after integer reduction.
 *
 * Feature processing is tiled and centroid search is blocked in two dimensions:
 * one query feature tile is reused against a bounded block of centroid tiles.
 * All hot-path work is caller-owned or fixed-size stack state; no heap allocation
 * occurs in these kernels. The small per-row accumulator block is 64-byte aligned
 * using the shared helper; actual cache-line sizes remain target-dependent.
 *
 * Cache policy for future maintainers:
 *   - 100 KiB is the nominal upper budget for a query tile, centroid tiles and
 *     a small scratch allowance, reduced from the former 500 KiB setting.
 *   - This bounds the software's intended active block; it cannot reserve cache
 *     or guarantee that data never accesses L2. L1/L2 sizes and replacement are
 *     specific to the CPU/SoC, and other tasks can evict cache lines.
 *   - A 4-row cap bounds accumulator state. With 4 KiB tiles, query + four row
 *     tiles is about 20 KiB plus scratch, a more L1-friendly target on many
 *     (not all) systems. The 100 KiB value is a ceiling, not the desired batch.
 *     Tune on target ARM hardware; desktop cache layouts do not generalize to SoCs.
 *
 * This translation unit is safe to compile on non-ARM machines. On such
 * machines it exports an unavailable backend table so generic builds can
 * include the file without NEON instructions or unresolved symbols.
 */

/* Keep the nominal budget consistent with the x86 and ESP-DSP backends. */
#define FIN_NEON_CACHE_BUDGET_BYTES (100U * 1024U)
#define FIN_NEON_FEATURE_TILE       4096U
#define FIN_NEON_CENTROID_TILE_MAX  4U
#define FIN_NEON_KERNEL_SCRATCH_BUDGET_BYTES 256U
#define FIN_NEON_MAX_U8_SQUARED_DIFF UINT64_C(65025)

#if (defined(__aarch64__) || defined(__ARM_NEON) || defined(__ARM_NEON__)) && \
    (defined(__GNUC__) || defined(__clang__))

#include <arm_neon.h>

#if defined(__linux__) && defined(__arm__) && !defined(__aarch64__)
#include <sys/auxv.h>
#ifndef HWCAP_NEON
#define HWCAP_NEON (1UL << 12)
#endif
#endif

#define FIN_VOICE_NEON_AVAILABLE 1

/* ------------------------------------------------------------------------- */
/* Validation and scaling                                                    */
/* ------------------------------------------------------------------------- */

static bool fin_neon_scale_valid(float scale)
{
    return isfinite(scale) && scale > 0.0f;
}

static bool fin_neon_scale_distance(
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

static bool fin_neon_scale_dot(
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

static bool fin_neon_valid_pair(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count)
{
    return count == 0U || (lhs != NULL && rhs != NULL);
}

static bool fin_neon_count_fits_dot(size_t count)
{
    return (uintmax_t)count <=
           ((uintmax_t)INT64_MAX / FIN_NEON_MAX_U8_SQUARED_DIFF);
}

static bool fin_neon_count_fits_distance(size_t count)
{
    return (uintmax_t)count <=
           ((uintmax_t)UINT64_MAX / FIN_NEON_MAX_U8_SQUARED_DIFF);
}

static bool fin_neon_valid_matrix(
    const uint8_t *query,
    const uint8_t *centroids,
    size_t centroid_count,
    size_t dimension,
    size_t *out_index,
    float *out_squared_distance_f32)
{
    if (query == NULL || centroids == NULL || out_index == NULL ||
        out_squared_distance_f32 == NULL || centroid_count == 0U ||
        dimension == 0U) {
        return false;
    }

    if (centroid_count > SIZE_MAX / dimension) {
        return false;
    }

    return fin_neon_count_fits_distance(dimension);
}

/* ------------------------------------------------------------------------- */
/* Small NEON reduction helpers                                              */
/* ------------------------------------------------------------------------- */

/*
 * Use constant-lane extracts instead of vaddvq_s32 so this source also works
 * with 32-bit ARM NEON toolchains, not only AArch64 toolchains.
 */
static int64_t fin_neon_reduce_i32x4(int32x4_t value)
{
    return (int64_t)vgetq_lane_s32(value, 0) +
           (int64_t)vgetq_lane_s32(value, 1) +
           (int64_t)vgetq_lane_s32(value, 2) +
           (int64_t)vgetq_lane_s32(value, 3);
}

/* ------------------------------------------------------------------------- */
/* One FP32-output dot-product feature tile                                   */
/* ------------------------------------------------------------------------- */

static int64_t fin_neon_dot_tile_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    uint8_t zero_point)
{
    const uint16x8_t zero_point_u16 = vdupq_n_u16((uint16_t)zero_point);
    int32x4_t accum0 = vdupq_n_s32(0);
    int32x4_t accum1 = vdupq_n_s32(0);
    int32x4_t accum2 = vdupq_n_s32(0);
    int32x4_t accum3 = vdupq_n_s32(0);
    size_t i = 0U;
    int64_t total;

    /* Each int32 lane receives at most count/16 products. With a tile of at
     * most 4096 bytes, this is far below INT32_MAX even at extreme UINT8 values. */
    for (; count - i >= 16U; i += 16U) {
        const uint8x16_t lhs_u8 = vld1q_u8(lhs + i);
        const uint8x16_t rhs_u8 = vld1q_u8(rhs + i);

        const uint16x8_t lhs_lo_u16 = vmovl_u8(vget_low_u8(lhs_u8));
        const uint16x8_t lhs_hi_u16 = vmovl_u8(vget_high_u8(lhs_u8));
        const uint16x8_t rhs_lo_u16 = vmovl_u8(vget_low_u8(rhs_u8));
        const uint16x8_t rhs_hi_u16 = vmovl_u8(vget_high_u8(rhs_u8));

        /* UINT16 subtraction wraps modulo 2^16; values are in [-255,255], so
         * reinterpretation as INT16 yields the intended centered operands. */
        const int16x8_t lhs_lo = vreinterpretq_s16_u16(
            vsubq_u16(lhs_lo_u16, zero_point_u16));
        const int16x8_t lhs_hi = vreinterpretq_s16_u16(
            vsubq_u16(lhs_hi_u16, zero_point_u16));
        const int16x8_t rhs_lo = vreinterpretq_s16_u16(
            vsubq_u16(rhs_lo_u16, zero_point_u16));
        const int16x8_t rhs_hi = vreinterpretq_s16_u16(
            vsubq_u16(rhs_hi_u16, zero_point_u16));

        accum0 = vaddq_s32(
            accum0,
            vmull_s16(vget_low_s16(lhs_lo), vget_low_s16(rhs_lo)));
        accum1 = vaddq_s32(
            accum1,
            vmull_s16(vget_high_s16(lhs_lo), vget_high_s16(rhs_lo)));
        accum2 = vaddq_s32(
            accum2,
            vmull_s16(vget_low_s16(lhs_hi), vget_low_s16(rhs_hi)));
        accum3 = vaddq_s32(
            accum3,
            vmull_s16(vget_high_s16(lhs_hi), vget_high_s16(rhs_hi)));
    }

    total = fin_neon_reduce_i32x4(accum0) +
            fin_neon_reduce_i32x4(accum1) +
            fin_neon_reduce_i32x4(accum2) +
            fin_neon_reduce_i32x4(accum3);

    /* Exact scalar tail for counts not divisible by 16. */
    for (; i < count; ++i) {
        const int32_t a = (int32_t)lhs[i] - (int32_t)zero_point;
        const int32_t b = (int32_t)rhs[i] - (int32_t)zero_point;
        total += (int64_t)(a * b);
    }

    return total;
}

/* ------------------------------------------------------------------------- */
/* One UINT64-output squared-distance feature tile                            */
/* ------------------------------------------------------------------------- */

static uint64_t fin_neon_distance_tile_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count)
{
    int32x4_t accum0 = vdupq_n_s32(0);
    int32x4_t accum1 = vdupq_n_s32(0);
    int32x4_t accum2 = vdupq_n_s32(0);
    int32x4_t accum3 = vdupq_n_s32(0);
    size_t i = 0U;
    uint64_t total;

    for (; count - i >= 16U; i += 16U) {
        const uint8x16_t lhs_u8 = vld1q_u8(lhs + i);
        const uint8x16_t rhs_u8 = vld1q_u8(rhs + i);

        const uint16x8_t lhs_lo_u16 = vmovl_u8(vget_low_u8(lhs_u8));
        const uint16x8_t lhs_hi_u16 = vmovl_u8(vget_high_u8(lhs_u8));
        const uint16x8_t rhs_lo_u16 = vmovl_u8(vget_low_u8(rhs_u8));
        const uint16x8_t rhs_hi_u16 = vmovl_u8(vget_high_u8(rhs_u8));

        const int16x8_t diff_lo = vreinterpretq_s16_u16(
            vsubq_u16(lhs_lo_u16, rhs_lo_u16));
        const int16x8_t diff_hi = vreinterpretq_s16_u16(
            vsubq_u16(lhs_hi_u16, rhs_hi_u16));

        accum0 = vaddq_s32(
            accum0,
            vmull_s16(vget_low_s16(diff_lo), vget_low_s16(diff_lo)));
        accum1 = vaddq_s32(
            accum1,
            vmull_s16(vget_high_s16(diff_lo), vget_high_s16(diff_lo)));
        accum2 = vaddq_s32(
            accum2,
            vmull_s16(vget_low_s16(diff_hi), vget_low_s16(diff_hi)));
        accum3 = vaddq_s32(
            accum3,
            vmull_s16(vget_high_s16(diff_hi), vget_high_s16(diff_hi)));
    }

    total = (uint64_t)(fin_neon_reduce_i32x4(accum0) +
                       fin_neon_reduce_i32x4(accum1) +
                       fin_neon_reduce_i32x4(accum2) +
                       fin_neon_reduce_i32x4(accum3));

    for (; i < count; ++i) {
        const int32_t difference = (int32_t)lhs[i] - (int32_t)rhs[i];
        total += (uint64_t)(difference * difference);
    }

    return total;
}

/* ------------------------------------------------------------------------- */
/* Exact integer aggregation over bounded feature tiles                       */
/* ------------------------------------------------------------------------- */

static bool fin_neon_dot_integer_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    uint8_t zero_point,
    int64_t *out_integer)
{
    size_t start = 0U;
    int64_t total = INT64_C(0);

    if (out_integer == NULL || !fin_neon_valid_pair(lhs, rhs, count) ||
        !fin_neon_count_fits_dot(count)) {
        return false;
    }

    while (start < count) {
        const size_t remaining = count - start;
        const size_t tile_count = remaining < (size_t)FIN_NEON_FEATURE_TILE
                                      ? remaining
                                      : (size_t)FIN_NEON_FEATURE_TILE;
        total += fin_neon_dot_tile_u8(
            lhs + start, rhs + start, tile_count, zero_point);
        start += tile_count;
    }

    *out_integer = total;
    return true;
}

static bool fin_neon_distance_integer_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    uint64_t *out_integer)
{
    size_t start = 0U;
    uint64_t total = UINT64_C(0);

    if (out_integer == NULL || !fin_neon_valid_pair(lhs, rhs, count) ||
        !fin_neon_count_fits_distance(count)) {
        return false;
    }

    while (start < count) {
        const size_t remaining = count - start;
        const size_t tile_count = remaining < (size_t)FIN_NEON_FEATURE_TILE
                                      ? remaining
                                      : (size_t)FIN_NEON_FEATURE_TILE;
        total += fin_neon_distance_tile_u8(
            lhs + start, rhs + start, tile_count);
        start += tile_count;
    }

    *out_integer = total;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Public operation implementations                                          */
/* ------------------------------------------------------------------------- */

static bool fin_neon_dot_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    uint8_t zero_point,
    float *out_f32)
{
    int64_t integer_dot = INT64_C(0);

    if (out_f32 == NULL || !fin_neon_scale_valid(scale)) {
        return false;
    }

    if (count == 0U) {
        *out_f32 = 0.0f;
        return true;
    }

    if (!fin_neon_dot_integer_u8(lhs, rhs, count, zero_point, &integer_dot)) {
        return false;
    }

    return fin_neon_scale_dot(integer_dot, scale, out_f32);
}

static bool fin_neon_squared_distance_u8(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    float *out_squared_distance_f32)
{
    uint64_t integer_distance = UINT64_C(0);

    if (out_squared_distance_f32 == NULL || !fin_neon_scale_valid(scale)) {
        return false;
    }

    if (count == 0U) {
        *out_squared_distance_f32 = 0.0f;
        return true;
    }

    if (!fin_neon_distance_integer_u8(
            lhs, rhs, count, &integer_distance)) {
        return false;
    }

    return fin_neon_scale_distance(
        integer_distance, scale, out_squared_distance_f32);
}

/*
 * Calculate a safe centroid block using streamed feature tiles, not the whole
 * vector length. One query tile, N centroid tiles and a scratch allowance fit
 * within the 100 KiB software budget when possible. The fixed 16-row ceiling
 * bounds stack state and avoids giant blocks for tiny feature dimensions.
 */
static size_t fin_neon_centroid_block_count(size_t dimension)
{
    size_t tile_bytes;
    size_t available;
    size_t count;

    if (dimension == 0U) {
        return 1U;
    }

    tile_bytes = dimension < (size_t)FIN_NEON_FEATURE_TILE
        ? dimension : (size_t)FIN_NEON_FEATURE_TILE;

    if (tile_bytes + (size_t)FIN_NEON_KERNEL_SCRATCH_BUDGET_BYTES >=
        (size_t)FIN_NEON_CACHE_BUDGET_BYTES) {
        return 1U;
    }

    available = (size_t)FIN_NEON_CACHE_BUDGET_BYTES - tile_bytes -
                (size_t)FIN_NEON_KERNEL_SCRATCH_BUDGET_BYTES;
    count = available / tile_bytes;

    if (count == 0U) {
        return 1U;
    }
    if (count > (size_t)FIN_NEON_CENTROID_TILE_MAX) {
        count = (size_t)FIN_NEON_CENTROID_TILE_MAX;
    }

    return count;
}

static bool fin_neon_nearest_u8(
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
    const size_t block_capacity = fin_neon_centroid_block_count(dimension);
    bool have_best = false;

    if (!fin_neon_scale_valid(scale) ||
        !fin_neon_valid_matrix(query, centroids, centroid_count, dimension,
                               out_index, out_squared_distance_f32)) {
        return false;
    }

    while (block_start < centroid_count) {
        const size_t remaining = centroid_count - block_start;
        const size_t block_count = remaining < block_capacity
                                       ? remaining
                                       : block_capacity;
        FIN_VOICE_CACHE_ALIGN uint64_t
            block_distances[FIN_NEON_CENTROID_TILE_MAX] = { UINT64_C(0) };
        size_t feature_offset = 0U;
        size_t local_row;

        /*
         * Tile features outside centroid rows. The same query slice is reused
         * for this block before moving to the next feature range. This is the
         * reason for the row-block helper above: it now controls the live access
         * window, rather than merely grouping a row-at-a-time loop.
         */
        while (feature_offset < dimension) {
            const size_t remaining_features = dimension - feature_offset;
            const size_t tile_count = remaining_features < (size_t)FIN_NEON_FEATURE_TILE
                ? remaining_features : (size_t)FIN_NEON_FEATURE_TILE;
            const uint8_t *const query_tile = query + feature_offset;

            for (local_row = 0U; local_row < block_count; ++local_row) {
                const size_t row = block_start + local_row;
                const uint8_t *const centroid_tile =
                    centroids + row * dimension + feature_offset;
                uint64_t tile_distance = UINT64_C(0);

                if (!fin_neon_distance_integer_u8(
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

            /* Integer-domain comparison is exact; strict < keeps earlier ties. */
            if (!have_best || distance < best_distance) {
                best_distance = distance;
                best_index = row;
                have_best = true;
            }
        }

        block_start += block_count;
    }

    if (!have_best ||
        !fin_neon_scale_distance(
            best_distance, scale, out_squared_distance_f32)) {
        return false;
    }

    *out_index = best_index;
    return true;
}

static bool fin_voice_simd_neon_is_supported(void)
{
#if defined(__aarch64__)
    /* Advanced SIMD/NEON is mandatory in AArch64. */
    return true;
#elif defined(__linux__) && defined(__arm__)
    /* On 32-bit Linux ARM, do not assume every CPU implements NEON. */
    return (getauxval(AT_HWCAP) & (unsigned long)HWCAP_NEON) != 0UL;
#else
    /* Other ARM targets reach this object only when built for NEON. */
    return true;
#endif
}

const FinVoiceSimdOps fin_voice_simd_neon_ops = {
    .backend = FIN_VOICE_SIMD_NEON,
    .name = "arm-neon-u8",
    .is_supported = fin_voice_simd_neon_is_supported,
    .dot_u8 = fin_neon_dot_u8,
    .squared_distance_u8 = fin_neon_squared_distance_u8,
    .nearest_u8 = fin_neon_nearest_u8
};

#else /* no compatible ARM NEON compiler target */

static bool fin_voice_simd_neon_is_supported(void)
{
    return false;
}

const FinVoiceSimdOps fin_voice_simd_neon_ops = {
    .backend = FIN_VOICE_SIMD_NEON,
    .name = "neon-unavailable",
    .is_supported = fin_voice_simd_neon_is_supported,
    .dot_u8 = NULL,
    .squared_distance_u8 = NULL,
    .nearest_u8 = NULL
};

#endif
