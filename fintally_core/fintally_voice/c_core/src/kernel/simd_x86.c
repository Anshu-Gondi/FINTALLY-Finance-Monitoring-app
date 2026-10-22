#include "kernel/simd_dispatch.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/*
 * x86 runtime-dispatched kernels for quantized UINT8 feature vectors.
 *
 * Data contract:
 *   - Inputs are UINT8 vectors produced by the caller's quantizer.
 *   - Dot products accumulate in signed INT64; squared distances in UINT64.
 *   - Scaling happens once after the integer reduction, yielding FP32 output.
 *   - Nearest-centroid ranking compares exact integer distances before scaling.
 *
 * Why the loops are tiled:
 *   - A feature tile bounds each kernel call and limits the lifetime of SIMD
 *     intermediates; the scalar tail handles lengths not divisible by vector width.
 *   - Centroid tiling reuses the same query feature tile across a small row block.
 *   - All scratch and accumulators are stack-local; these kernels allocate no heap.
 *   - Vector store scratch is explicitly 64-byte aligned through the shared C23
 *     alignment macro. SIMD source vectors still use unaligned loads because
 *     callers are not required to provide specially aligned UINT8 arrays.
 *
 * Cache-budget note for future maintainers:
 *   - 100 KiB is a SOFTWARE bound for the nominal query tile + centroid tiles +
 *     a conservative scratch allowance. It is not a hardware cache reservation. The CPU can
 *     still evict lines or access L2; cache size, associativity, prefetching and
 *     competing threads are architecture/runtime dependent.
 *   - This lower bound is intended to reduce the candidate working set relative
 *     to the former 500 KiB setting, not to promise L1 residency or zero L2 use.
 *   - The row block is capped at 4, even though the nominal budget is larger.
 *     With 4 KiB tiles this keeps the input window near 20 KiB plus scratch,
 *     a more L1-friendly target on many (not all) CPUs than a 100 KiB batch.
 *     Treat the 100 KiB value as a hard software ceiling, not the desired size.
 *     Benchmark on the target CPU before changing the tile or row cap.
 *
 * Build this file against a generic x86 baseline. Do NOT apply -march=native to
 * the whole target: target attributes isolate instructions to each specialized
 * function, allowing runtime dispatch to fall back safely on older CPUs.
 */

#if (defined(__x86_64__) || defined(__i386__)) && \
    (defined(__GNUC__) || defined(__clang__))

#include <immintrin.h>

#define FIN_TARGET_SSE42 __attribute__((target("sse4.2")))
#define FIN_TARGET_AVX2 __attribute__((target("avx2")))
#define FIN_TARGET_AVX2_FMA __attribute__((target("avx2,fma")))
#define FIN_TARGET_AVX_VNNI __attribute__((target("avx2,avxvnni")))
#define FIN_TARGET_AVX512 __attribute__((target("avx2,avx512f,avx512bw")))

/* Keep this nominal policy aligned with the NEON and ESP-DSP backends.
 * 100 KiB = 102,400 bytes. The feature tile is a stream window, not a full
 * vector allocation; the separate row cap bounds the nearest-search accumulator. */
#define FIN_X86_CACHE_BUDGET_BYTES (100U * 1024U)
#define FIN_X86_FEATURE_TILE 4096U
#define FIN_X86_CENTROID_TILE_MAX 4U
#define FIN_X86_KERNEL_SCRATCH_BUDGET_BYTES 512U
#define FIN_X86_MAX_U8_DISTANCE UINT64_C(65025)

typedef uint64_t (*FinVoiceDistanceChunkFn)(
    const uint8_t *lhs, const uint8_t *rhs, size_t count);
typedef int64_t (*FinVoiceDotChunkFn)(
    const uint8_t *lhs, const uint8_t *rhs, size_t count, uint8_t zero_point);

static bool fin_x86_valid_scale(float scale)
{
    return isfinite(scale) && scale > 0.0f;
}

static bool fin_x86_valid_pair(const uint8_t *lhs, const uint8_t *rhs, size_t n)
{
    return n == 0U || (lhs != NULL && rhs != NULL);
}

static bool fin_x86_valid_matrix(
    const uint8_t *query, const uint8_t *centroids,
    size_t centroid_count, size_t dimension,
    size_t *out_index, float *out_distance)
{
    if (query == NULL || centroids == NULL || out_index == NULL ||
        out_distance == NULL || centroid_count == 0U || dimension == 0U) {
        return false;
    }
    if (centroid_count > SIZE_MAX / dimension) {
        return false;
    }
    if (dimension > UINT64_MAX / FIN_X86_MAX_U8_DISTANCE) {
        return false;
    }
    return true;
}

static bool fin_x86_scale_distance(uint64_t integer_distance, float scale, float *out)
{
    const float scaled = (float)integer_distance * scale * scale;
    if (!isfinite(scaled)) {
        return false;
    }
    *out = scaled;
    return true;
}

static bool fin_x86_scale_dot(int64_t integer_dot, float scale, float *out)
{
    const float scaled = (float)integer_dot * scale * scale;
    if (!isfinite(scaled)) {
        return false;
    }
    *out = scaled;
    return true;
}

static uint64_t fin_x86_distance_scalar_chunk(
    const uint8_t *lhs, const uint8_t *rhs, size_t count)
{
    uint64_t total = UINT64_C(0);
    size_t i;
    for (i = 0U; i < count; ++i) {
        const int32_t d = (int32_t)lhs[i] - (int32_t)rhs[i];
        total += (uint64_t)(d * d);
    }
    return total;
}

static int64_t fin_x86_dot_scalar_chunk(
    const uint8_t *lhs, const uint8_t *rhs, size_t count, uint8_t zp)
{
    int64_t total = INT64_C(0);
    size_t i;
    for (i = 0U; i < count; ++i) {
        const int32_t a = (int32_t)lhs[i] - (int32_t)zp;
        const int32_t b = (int32_t)rhs[i] - (int32_t)zp;
        total += (int64_t)(a * b);
    }
    return total;
}

/* ------------------------------------------------------------------------- */
/* SSE4.2 distance: unsigned byte absolute differences, then exact squares.  */
/* SSE2 instructions are used inside the SSE4.2 target function.             */
/* ------------------------------------------------------------------------- */

FIN_TARGET_SSE42
static uint64_t fin_x86_distance_sse42_chunk(
    const uint8_t *lhs, const uint8_t *rhs, size_t count)
{
    uint64_t total = UINT64_C(0);
    size_t i = 0U;
    const __m128i zero = _mm_setzero_si128();
    FIN_VOICE_CACHE_ALIGN uint16_t squares[16];

    for (; count - i >= 16U; i += 16U) {
        const __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(lhs + i));
        const __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(rhs + i));
        const __m128i high = _mm_max_epu8(a, b);
        const __m128i low = _mm_min_epu8(a, b);
        const __m128i diff = _mm_sub_epi8(high, low);
        const __m128i lo16 = _mm_unpacklo_epi8(diff, zero);
        const __m128i hi16 = _mm_unpackhi_epi8(diff, zero);
        const __m128i sq_lo = _mm_mullo_epi16(lo16, lo16);
        const __m128i sq_hi = _mm_mullo_epi16(hi16, hi16);
        size_t j;
        _mm_store_si128((__m128i *)(void *)squares, sq_lo);
        _mm_store_si128((__m128i *)(void *)(squares + 8), sq_hi);
        for (j = 0U; j < 16U; ++j) {
            total += (uint64_t)squares[j];
        }
    }
    total += fin_x86_distance_scalar_chunk(lhs + i, rhs + i, count - i);
    return total;
}

/* SSE4.2 signed dot product. Each madd result is bounded; each feature tile
 * is capped at 4096 values to keep the 32-bit vector accumulators safe. */
FIN_TARGET_SSE42
static int64_t fin_x86_dot_sse42_chunk(
    const uint8_t *lhs, const uint8_t *rhs, size_t count, uint8_t zp)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i zpv = _mm_set1_epi16((short)zp);
    __m128i accum_lo = _mm_setzero_si128();
    __m128i accum_hi = _mm_setzero_si128();
    int64_t total = INT64_C(0);
    size_t i = 0U;
    int32_t lanes_lo[4];
    int32_t lanes_hi[4];

    for (; count - i >= 16U; i += 16U) {
        const __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(lhs + i));
        const __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(rhs + i));
        const __m128i a_lo = _mm_sub_epi16(_mm_unpacklo_epi8(a, zero), zpv);
        const __m128i a_hi = _mm_sub_epi16(_mm_unpackhi_epi8(a, zero), zpv);
        const __m128i b_lo = _mm_sub_epi16(_mm_unpacklo_epi8(b, zero), zpv);
        const __m128i b_hi = _mm_sub_epi16(_mm_unpackhi_epi8(b, zero), zpv);
        accum_lo = _mm_add_epi32(accum_lo, _mm_madd_epi16(a_lo, b_lo));
        accum_hi = _mm_add_epi32(accum_hi, _mm_madd_epi16(a_hi, b_hi));
    }
    _mm_storeu_si128((__m128i *)(void *)lanes_lo, accum_lo);
    _mm_storeu_si128((__m128i *)(void *)lanes_hi, accum_hi);
    {
        size_t j;
        for (j = 0U; j < 4U; ++j) {
            total += lanes_lo[j];
            total += lanes_hi[j];
        }
    }
    total += fin_x86_dot_scalar_chunk(lhs + i, rhs + i, count - i, zp);
    return total;
}

/* ------------------------------------------------------------------------- */
/* AVX2 distance and dot product.                                            */
/* ------------------------------------------------------------------------- */

FIN_TARGET_AVX2
static uint64_t fin_x86_distance_avx2_chunk(
    const uint8_t *lhs, const uint8_t *rhs, size_t count)
{
    uint64_t total = UINT64_C(0);
    size_t i = 0U;
    FIN_VOICE_CACHE_ALIGN uint16_t squares[32];

    for (; count - i >= 32U; i += 32U) {
        const __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(lhs + i));
        const __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(rhs + i));
        const __m256i high = _mm256_max_epu8(a, b);
        const __m256i low = _mm256_min_epu8(a, b);
        const __m256i diff = _mm256_sub_epi8(high, low);
        const __m128i diff_lo = _mm256_castsi256_si128(diff);
        const __m128i diff_hi = _mm256_extracti128_si256(diff, 1);
        const __m256i lo16 = _mm256_cvtepu8_epi16(diff_lo);
        const __m256i hi16 = _mm256_cvtepu8_epi16(diff_hi);
        const __m256i sq_lo = _mm256_mullo_epi16(lo16, lo16);
        const __m256i sq_hi = _mm256_mullo_epi16(hi16, hi16);
        size_t j;
        _mm256_store_si256((__m256i *)(void *)squares, sq_lo);
        _mm256_store_si256((__m256i *)(void *)(squares + 16), sq_hi);
        for (j = 0U; j < 32U; ++j) {
            total += (uint64_t)squares[j];
        }
    }
    total += fin_x86_distance_scalar_chunk(lhs + i, rhs + i, count - i);
    return total;
}

FIN_TARGET_AVX2
static int64_t fin_x86_dot_avx2_chunk(
    const uint8_t *lhs, const uint8_t *rhs, size_t count, uint8_t zp)
{
    const __m256i zpv = _mm256_set1_epi16((short)zp);
    __m256i accum_lo = _mm256_setzero_si256();
    __m256i accum_hi = _mm256_setzero_si256();
    int64_t total = INT64_C(0);
    size_t i = 0U;
    int32_t lanes_lo[8];
    int32_t lanes_hi[8];

    for (; count - i >= 32U; i += 32U) {
        const __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(lhs + i));
        const __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(rhs + i));
        const __m128i a0_8 = _mm256_castsi256_si128(a);
        const __m128i a1_8 = _mm256_extracti128_si256(a, 1);
        const __m128i b0_8 = _mm256_castsi256_si128(b);
        const __m128i b1_8 = _mm256_extracti128_si256(b, 1);
        const __m256i a0 = _mm256_sub_epi16(_mm256_cvtepu8_epi16(a0_8), zpv);
        const __m256i a1 = _mm256_sub_epi16(_mm256_cvtepu8_epi16(a1_8), zpv);
        const __m256i b0 = _mm256_sub_epi16(_mm256_cvtepu8_epi16(b0_8), zpv);
        const __m256i b1 = _mm256_sub_epi16(_mm256_cvtepu8_epi16(b1_8), zpv);
        accum_lo = _mm256_add_epi32(accum_lo, _mm256_madd_epi16(a0, b0));
        accum_hi = _mm256_add_epi32(accum_hi, _mm256_madd_epi16(a1, b1));
    }
    _mm256_storeu_si256((__m256i *)(void *)lanes_lo, accum_lo);
    _mm256_storeu_si256((__m256i *)(void *)lanes_hi, accum_hi);
    {
        size_t j;
        for (j = 0U; j < 8U; ++j) {
            total += lanes_lo[j];
            total += lanes_hi[j];
        }
    }
    total += fin_x86_dot_scalar_chunk(lhs + i, rhs + i, count - i, zp);
    return total;
}

/* ------------------------------------------------------------------------- */
/* AVX-VNNI dot product. VPDPBUSD handles unsigned x signed byte products.    */
/* Split rhs into its low 7 bits and high bit to represent all UINT8 values.  */
/* Centering is then applied algebraically, exactly, in INT64.               */
/* ------------------------------------------------------------------------- */

FIN_TARGET_AVX_VNNI
static int64_t fin_x86_dot_avx_vnni_chunk(
    const uint8_t *lhs, const uint8_t *rhs, size_t count, uint8_t zp)
{
    const __m256i mask7f = _mm256_set1_epi8(0x7f);
    const __m256i mask80 = _mm256_set1_epi8((char)0x80);
    const __m256i zero = _mm256_setzero_si256();
    __m256i acc_low = _mm256_setzero_si256();
    __m256i acc_high = _mm256_setzero_si256();
    __m256i sum_a_vec = _mm256_setzero_si256();
    __m256i sum_b_vec = _mm256_setzero_si256();
    uint64_t sum_a_lanes[4];
    uint64_t sum_b_lanes[4];
    int32_t low_lanes[8];
    int32_t high_lanes[8];
    uint64_t sum_a = UINT64_C(0);
    uint64_t sum_b = UINT64_C(0);
    int64_t raw_dot = INT64_C(0);
    int64_t result;
    size_t i = 0U;

    for (; count - i >= 32U; i += 32U) {
        const __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(lhs + i));
        const __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(rhs + i));
        const __m256i b_low = _mm256_and_si256(b, mask7f);
        const __m256i b_high = _mm256_srli_epi16(_mm256_and_si256(b, mask80), 7);

        acc_low = _mm256_dpbusd_avx_epi32(acc_low, a, b_low);
        acc_high = _mm256_dpbusd_avx_epi32(acc_high, a, b_high);
        sum_a_vec = _mm256_add_epi64(sum_a_vec, _mm256_sad_epu8(a, zero));
        sum_b_vec = _mm256_add_epi64(sum_b_vec, _mm256_sad_epu8(b, zero));
    }

    _mm256_storeu_si256((__m256i *)(void *)low_lanes, acc_low);
    _mm256_storeu_si256((__m256i *)(void *)high_lanes, acc_high);
    _mm256_storeu_si256((__m256i *)(void *)sum_a_lanes, sum_a_vec);
    _mm256_storeu_si256((__m256i *)(void *)sum_b_lanes, sum_b_vec);
    {
        size_t j;
        for (j = 0U; j < 8U; ++j) {
            raw_dot += low_lanes[j];
            raw_dot += (int64_t)128 * high_lanes[j];
        }
        for (j = 0U; j < 4U; ++j) {
            sum_a += sum_a_lanes[j];
            sum_b += sum_b_lanes[j];
        }
    }

    /* Correct the dot product for this vectorized prefix. */
    {
        const int64_t zp64 = (int64_t)zp;
        const int64_t prefix_count = (int64_t)i;
        result = raw_dot - zp64 * (int64_t)sum_a - zp64 * (int64_t)sum_b
                 + prefix_count * zp64 * zp64;
    }
    /* Add the unvectorized tail directly in centered arithmetic. */
    result += fin_x86_dot_scalar_chunk(lhs + i, rhs + i, count - i, zp);
    return result;
}

/* ------------------------------------------------------------------------- */
/* AVX-512BW distance. Vectors are processed 64 bytes at a time.             */
/* ------------------------------------------------------------------------- */

FIN_TARGET_AVX512
static uint64_t fin_x86_distance_avx512_chunk(
    const uint8_t *lhs, const uint8_t *rhs, size_t count)
{
    uint64_t total = UINT64_C(0);
    size_t i = 0U;
    FIN_VOICE_CACHE_ALIGN uint16_t squares[64];

    for (; count - i >= 64U; i += 64U) {
        const __m512i a = _mm512_loadu_si512((const void *)(lhs + i));
        const __m512i b = _mm512_loadu_si512((const void *)(rhs + i));
        const __m512i high = _mm512_max_epu8(a, b);
        const __m512i low = _mm512_min_epu8(a, b);
        const __m512i diff = _mm512_sub_epi8(high, low);
        const __m256i diff_lo = _mm512_castsi512_si256(diff);
        const __m256i diff_hi = _mm512_extracti64x4_epi64(diff, 1);
        const __m512i lo16 = _mm512_cvtepu8_epi16(diff_lo);
        const __m512i hi16 = _mm512_cvtepu8_epi16(diff_hi);
        const __m512i sq_lo = _mm512_mullo_epi16(lo16, lo16);
        const __m512i sq_hi = _mm512_mullo_epi16(hi16, hi16);
        size_t j;
        _mm512_store_si512((void *)squares, sq_lo);
        _mm512_store_si512((void *)(squares + 32), sq_hi);
        for (j = 0U; j < 64U; ++j) {
            total += (uint64_t)squares[j];
        }
    }
    total += fin_x86_distance_scalar_chunk(lhs + i, rhs + i, count - i);
    return total;
}

/* ------------------------------------------------------------------------- */
/* Shared tiled operations.                                                   */
/*                                                                           */
/* Each feature tile is at most 4 KiB per input. Nearest-centroid search now  */
/* walks the feature tile first, across a bounded block of centroid rows.    */
/* That makes the row block a real access-order tile rather than just an      */
/* outer-loop grouping. The nominal query + row tiles + scratch allowance    */
/* stays at or below 100 KiB; the exact cache behavior remains hardware-owned. */
/* ------------------------------------------------------------------------- */

static bool fin_x86_dot_u8_with_kernel(
    const uint8_t *lhs, const uint8_t *rhs, size_t count,
    float scale, uint8_t zero_point, float *out, FinVoiceDotChunkFn kernel)
{
    size_t offset = 0U;
    int64_t total = INT64_C(0);

    if (out == NULL || !fin_x86_valid_scale(scale) ||
        !fin_x86_valid_pair(lhs, rhs, count) || kernel == NULL) {
        return false;
    }
    if ((uint64_t)count > (uint64_t)(INT64_MAX / INT64_C(65025))) {
        return false;
    }
    while (offset < count) {
        const size_t remaining = count - offset;
        const size_t tile = remaining < (size_t)FIN_X86_FEATURE_TILE
                          ? remaining : (size_t)FIN_X86_FEATURE_TILE;
        total += kernel(lhs + offset, rhs + offset, tile, zero_point);
        offset += tile;
    }
    return fin_x86_scale_dot(total, scale, out);
}

static bool fin_x86_distance_u8_with_kernel(
    const uint8_t *lhs, const uint8_t *rhs, size_t count,
    float scale, float *out, FinVoiceDistanceChunkFn kernel)
{
    size_t offset = 0U;
    uint64_t total = UINT64_C(0);

    if (out == NULL || !fin_x86_valid_scale(scale) ||
        !fin_x86_valid_pair(lhs, rhs, count) || kernel == NULL) {
        return false;
    }
    if (count > UINT64_MAX / FIN_X86_MAX_U8_DISTANCE) {
        return false;
    }
    while (offset < count) {
        const size_t remaining = count - offset;
        const size_t tile = remaining < (size_t)FIN_X86_FEATURE_TILE
                          ? remaining : (size_t)FIN_X86_FEATURE_TILE;
        total += kernel(lhs + offset, rhs + offset, tile);
        offset += tile;
    }
    return fin_x86_scale_distance(total, scale, out);
}

/*
 * Determine the number of centroid feature tiles processed with one query
 * tile. Use min(dimension, FEATURE_TILE), because large vectors are streamed
 * in pieces: the full row is not resident in the active cache window at once.
 * Subtract the query tile and a conservative stack-temporary allowance first.
 */
static size_t fin_x86_centroid_rows_per_block(size_t dimension)
{
    size_t tile_bytes;
    size_t available;
    size_t rows;

    if (dimension == 0U) {
        return 1U;
    }

    tile_bytes = dimension < (size_t)FIN_X86_FEATURE_TILE
        ? dimension : (size_t)FIN_X86_FEATURE_TILE;

    if (tile_bytes + (size_t)FIN_X86_KERNEL_SCRATCH_BUDGET_BYTES >=
        (size_t)FIN_X86_CACHE_BUDGET_BYTES) {
        return 1U;
    }

    available = (size_t)FIN_X86_CACHE_BUDGET_BYTES - tile_bytes -
                (size_t)FIN_X86_KERNEL_SCRATCH_BUDGET_BYTES;
    rows = available / tile_bytes;

    if (rows == 0U) {
        return 1U;
    }
    if (rows > (size_t)FIN_X86_CENTROID_TILE_MAX) {
        rows = (size_t)FIN_X86_CENTROID_TILE_MAX;
    }

    return rows;
}

static bool fin_x86_nearest_u8_with_kernel(
    const uint8_t *query, const uint8_t *centroids,
    size_t centroid_count, size_t dimension, float scale,
    size_t *out_index, float *out_distance, FinVoiceDistanceChunkFn kernel)
{
    size_t centroid_block_start = 0U;
    size_t best_index = 0U;
    uint64_t best_integer_distance = UINT64_MAX;
    const size_t block_rows = dimension == 0U
        ? 1U : fin_x86_centroid_rows_per_block(dimension);
    bool have_best = false;

    if (!fin_x86_valid_scale(scale) ||
        !fin_x86_valid_matrix(query, centroids, centroid_count, dimension,
                              out_index, out_distance) || kernel == NULL) {
        return false;
    }

    while (centroid_block_start < centroid_count) {
        const size_t remaining_rows = centroid_count - centroid_block_start;
        const size_t row_count = remaining_rows < block_rows
                               ? remaining_rows : block_rows;
        /* Cache-line alignment keeps this small hot accumulator block from
         * straddling lines on common 64-byte-line targets. */
        FIN_VOICE_CACHE_ALIGN uint64_t row_distances[FIN_X86_CENTROID_TILE_MAX] =
            { UINT64_C(0) };
        size_t feature_offset = 0U;
        size_t local_row;

        /*
         * True two-dimensional blocking: reuse one query slice while visiting
         * the same feature range in up to 4 adjacent centroid rows. This
         * improves access locality over walking an entire very wide row before
         * starting the next row. The accumulator array has a fixed stack size.
         */
        while (feature_offset < dimension) {
            const size_t remaining_features = dimension - feature_offset;
            const size_t tile = remaining_features < (size_t)FIN_X86_FEATURE_TILE
                              ? remaining_features : (size_t)FIN_X86_FEATURE_TILE;
            const uint8_t *const query_tile = query + feature_offset;

            for (local_row = 0U; local_row < row_count; ++local_row) {
                const size_t row = centroid_block_start + local_row;
                const uint8_t *const centroid_tile =
                    centroids + row * dimension + feature_offset;

                row_distances[local_row] +=
                    kernel(query_tile, centroid_tile, tile);
            }

            feature_offset += tile;
        }

        for (local_row = 0U; local_row < row_count; ++local_row) {
            const size_t row = centroid_block_start + local_row;
            const uint64_t distance = row_distances[local_row];

            /* Strict comparison keeps the earliest row on equal distances. */
            if (!have_best || distance < best_integer_distance) {
                best_integer_distance = distance;
                best_index = row;
                have_best = true;
            }
        }

        centroid_block_start += row_count;
    }

    if (!have_best || !fin_x86_scale_distance(best_integer_distance, scale, out_distance)) {
        return false;
    }
    *out_index = best_index;
    return true;
}

/* ------------------------------------------------------------------------- */
/* ISA wrappers                                                              */
/* ------------------------------------------------------------------------- */

static bool fin_x86_sse42_supported(void)
{
    __builtin_cpu_init();
    return __builtin_cpu_supports("sse4.2") != 0;
}
static bool fin_x86_avx2_supported(void)
{
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") != 0;
}
static bool fin_x86_avx2_fma_supported(void)
{
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") != 0 &&
           __builtin_cpu_supports("fma") != 0;
}
static bool fin_x86_avx_vnni_supported(void)
{
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") != 0 &&
           __builtin_cpu_supports("avxvnni") != 0;
}
static bool fin_x86_avx512_supported(void)
{
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") != 0 &&
           __builtin_cpu_supports("avx512f") != 0 &&
           __builtin_cpu_supports("avx512bw") != 0;
}

static bool fin_x86_sse42_dot(const uint8_t *a, const uint8_t *b, size_t n,
                              float s, uint8_t z, float *o)
{ return fin_x86_dot_u8_with_kernel(a, b, n, s, z, o, fin_x86_dot_sse42_chunk); }
static bool fin_x86_sse42_dist(const uint8_t *a, const uint8_t *b, size_t n,
                               float s, float *o)
{ return fin_x86_distance_u8_with_kernel(a, b, n, s, o, fin_x86_distance_sse42_chunk); }
static bool fin_x86_sse42_nearest(const uint8_t *q, const uint8_t *c, size_t n,
                                  size_t d, float s, size_t *i, float *o)
{ return fin_x86_nearest_u8_with_kernel(q, c, n, d, s, i, o, fin_x86_distance_sse42_chunk); }

static bool fin_x86_avx2_dot(const uint8_t *a, const uint8_t *b, size_t n,
                             float s, uint8_t z, float *o)
{ return fin_x86_dot_u8_with_kernel(a, b, n, s, z, o, fin_x86_dot_avx2_chunk); }
static bool fin_x86_avx2_dist(const uint8_t *a, const uint8_t *b, size_t n,
                              float s, float *o)
{ return fin_x86_distance_u8_with_kernel(a, b, n, s, o, fin_x86_distance_avx2_chunk); }
static bool fin_x86_avx2_nearest(const uint8_t *q, const uint8_t *c, size_t n,
                                 size_t d, float s, size_t *i, float *o)
{ return fin_x86_nearest_u8_with_kernel(q, c, n, d, s, i, o, fin_x86_distance_avx2_chunk); }

static bool fin_x86_vnni_dot(const uint8_t *a, const uint8_t *b, size_t n,
                             float s, uint8_t z, float *o)
{ return fin_x86_dot_u8_with_kernel(a, b, n, s, z, o, fin_x86_dot_avx_vnni_chunk); }
static bool fin_x86_vnni_dist(const uint8_t *a, const uint8_t *b, size_t n,
                              float s, float *o)
{ return fin_x86_distance_u8_with_kernel(a, b, n, s, o, fin_x86_distance_avx2_chunk); }
static bool fin_x86_vnni_nearest(const uint8_t *q, const uint8_t *c, size_t n,
                                 size_t d, float s, size_t *i, float *o)
{ return fin_x86_nearest_u8_with_kernel(q, c, n, d, s, i, o, fin_x86_distance_avx2_chunk); }

static bool fin_x86_avx512_dot(const uint8_t *a, const uint8_t *b, size_t n,
                               float s, uint8_t z, float *o)
{
    /* Prefer AVX-VNNI for the byte dot product when that separate extension
       is present; AVX-512BW is still used for distance/search kernels. */
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avxvnni") != 0) {
        return fin_x86_dot_u8_with_kernel(
            a, b, n, s, z, o, fin_x86_dot_avx_vnni_chunk);
    }
    return fin_x86_dot_u8_with_kernel(
        a, b, n, s, z, o, fin_x86_dot_avx2_chunk);
}
static bool fin_x86_avx512_dist(const uint8_t *a, const uint8_t *b, size_t n,
                                float s, float *o)
{ return fin_x86_distance_u8_with_kernel(a, b, n, s, o, fin_x86_distance_avx512_chunk); }
static bool fin_x86_avx512_nearest(const uint8_t *q, const uint8_t *c, size_t n,
                                   size_t d, float s, size_t *i, float *o)
{ return fin_x86_nearest_u8_with_kernel(q, c, n, d, s, i, o, fin_x86_distance_avx512_chunk); }

/* AVX2+FMA is exposed as its own capability level. FMA is not useful for the
 * UINT8-only arithmetic; these operations intentionally reuse exact AVX2
 * integer kernels. The capability can be used for future FP32 kernels. */
static bool fin_x86_avx2_fma_dot(const uint8_t *a, const uint8_t *b, size_t n,
                                 float s, uint8_t z, float *o)
{ return fin_x86_avx2_dot(a, b, n, s, z, o); }
static bool fin_x86_avx2_fma_dist(const uint8_t *a, const uint8_t *b, size_t n,
                                  float s, float *o)
{ return fin_x86_avx2_dist(a, b, n, s, o); }
static bool fin_x86_avx2_fma_nearest(const uint8_t *q, const uint8_t *c, size_t n,
                                     size_t d, float s, size_t *i, float *o)
{ return fin_x86_avx2_nearest(q, c, n, d, s, i, o); }

const FinVoiceSimdOps fin_voice_simd_sse42_ops = {
    .backend = FIN_VOICE_SIMD_SSE42, .name = "sse4.2",
    .is_supported = fin_x86_sse42_supported,
    .dot_u8 = fin_x86_sse42_dot,
    .squared_distance_u8 = fin_x86_sse42_dist,
    .nearest_u8 = fin_x86_sse42_nearest
};
const FinVoiceSimdOps fin_voice_simd_avx2_ops = {
    .backend = FIN_VOICE_SIMD_AVX2, .name = "avx2",
    .is_supported = fin_x86_avx2_supported,
    .dot_u8 = fin_x86_avx2_dot,
    .squared_distance_u8 = fin_x86_avx2_dist,
    .nearest_u8 = fin_x86_avx2_nearest
};
const FinVoiceSimdOps fin_voice_simd_avx2_fma_ops = {
    .backend = FIN_VOICE_SIMD_AVX2_FMA, .name = "avx2+fma",
    .is_supported = fin_x86_avx2_fma_supported,
    .dot_u8 = fin_x86_avx2_fma_dot,
    .squared_distance_u8 = fin_x86_avx2_fma_dist,
    .nearest_u8 = fin_x86_avx2_fma_nearest
};
const FinVoiceSimdOps fin_voice_simd_avx_vnni_ops = {
    .backend = FIN_VOICE_SIMD_AVX_VNNI, .name = "avx2-vnni",
    .is_supported = fin_x86_avx_vnni_supported,
    .dot_u8 = fin_x86_vnni_dot,
    .squared_distance_u8 = fin_x86_vnni_dist,
    .nearest_u8 = fin_x86_vnni_nearest
};
const FinVoiceSimdOps fin_voice_simd_avx512_ops = {
    .backend = FIN_VOICE_SIMD_AVX512, .name = "avx512bw",
    .is_supported = fin_x86_avx512_supported,
    .dot_u8 = fin_x86_avx512_dot,
    .squared_distance_u8 = fin_x86_avx512_dist,
    .nearest_u8 = fin_x86_avx512_nearest
};

#else /* non-x86 or unsupported compiler */

/* Stable unavailable tables keep this file portable in multi-platform builds.
 * These backends are never selected because their support checks return false. */
static bool fin_x86_backend_unavailable(void) { return false; }
const FinVoiceSimdOps fin_voice_simd_sse42_ops = {
    .backend = FIN_VOICE_SIMD_SSE42, .name = "sse4.2-unavailable",
    .is_supported = fin_x86_backend_unavailable,
    .dot_u8 = NULL, .squared_distance_u8 = NULL, .nearest_u8 = NULL
};
const FinVoiceSimdOps fin_voice_simd_avx2_ops = {
    .backend = FIN_VOICE_SIMD_AVX2, .name = "avx2-unavailable",
    .is_supported = fin_x86_backend_unavailable,
    .dot_u8 = NULL, .squared_distance_u8 = NULL, .nearest_u8 = NULL
};
const FinVoiceSimdOps fin_voice_simd_avx2_fma_ops = {
    .backend = FIN_VOICE_SIMD_AVX2_FMA, .name = "avx2-fma-unavailable",
    .is_supported = fin_x86_backend_unavailable,
    .dot_u8 = NULL, .squared_distance_u8 = NULL, .nearest_u8 = NULL
};
const FinVoiceSimdOps fin_voice_simd_avx_vnni_ops = {
    .backend = FIN_VOICE_SIMD_AVX_VNNI, .name = "avx-vnni-unavailable",
    .is_supported = fin_x86_backend_unavailable,
    .dot_u8 = NULL, .squared_distance_u8 = NULL, .nearest_u8 = NULL
};
const FinVoiceSimdOps fin_voice_simd_avx512_ops = {
    .backend = FIN_VOICE_SIMD_AVX512, .name = "avx512-unavailable",
    .is_supported = fin_x86_backend_unavailable,
    .dot_u8 = NULL, .squared_distance_u8 = NULL, .nearest_u8 = NULL
};

#endif
