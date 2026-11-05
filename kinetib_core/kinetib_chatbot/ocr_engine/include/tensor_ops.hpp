#ifndef TENSOR_OPS_HPP
#define TENSOR_OPS_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#if defined(_MSC_VER)
    #include <malloc.h>
    #include <immintrin.h>
#else
    #include <immintrin.h>
#endif

namespace fin::ops {

// =============================================================================
// Aligned allocation
// =============================================================================

inline void* aligned_alloc(
    std::size_t alignment,
    std::size_t size
) noexcept {

#if defined(_MSC_VER) || defined(__MINGW32__)

    return _aligned_malloc(
        size,
        alignment
    );

#else

    void* ptr = nullptr;

    if (
        posix_memalign(
            &ptr,
            alignment,
            size
        ) != 0
    ) {
        return nullptr;
    }

    return ptr;

#endif
}

inline void aligned_free(
    void* ptr
) noexcept {

#if defined(_MSC_VER) || defined(__MINGW32__)

    _aligned_free(ptr);

#else

    std::free(ptr);

#endif
}

// =============================================================================
// Helpers
// =============================================================================

inline bool is_aligned(
    const void* ptr,
    std::size_t alignment
) noexcept {

    if (
        ptr == nullptr ||
        alignment == 0 ||
        (alignment & (alignment - 1)) != 0
    ) {
        return false;
    }

    return (
        reinterpret_cast<std::uintptr_t>(ptr)
        & (alignment - 1)
    ) == 0;
}

// =============================================================================
// SSE2 unsigned uint16_t minimum
// =============================================================================
//
// SSE2 does not provide _mm_min_epu16().
// That intrinsic is SSE4.1.
//
// Our values are known to be in the non-negative int16_t range, so a signed
// comparison is sufficient here.
//
// result = min(a, b)
// =============================================================================

#if defined(__SSE2__) && !defined(__SSE4_1__)

inline __m128i min_u16_sse2(
    __m128i a,
    __m128i b
) noexcept {

    const __m128i greater =
        _mm_cmpgt_epi16(
            a,
            b
        );

    return _mm_or_si128(
        _mm_andnot_si128(
            greater,
            a
        ),
        _mm_and_si128(
            greater,
            b
        )
    );
}

#endif

// =============================================================================
// Binarization
//
// result = 255 if pixel > 128
//          0 otherwise
//
// Supports:
//
//   AVX-512BW
//   AVX2
//   SSE2
//   scalar
//
// Unaligned memory is intentionally supported.
// =============================================================================

inline void binarize_simd(
    std::uint8_t* __restrict__ data,
    std::size_t length
) noexcept {

    if (
        data == nullptr ||
        length == 0
    ) {
        return;
    }

    std::size_t i = 0;

#if defined(__AVX512BW__)

    // =========================================================================
    // AVX-512BW
    // =========================================================================

    const __m512i threshold =
        _mm512_set1_epi8(
            static_cast<char>(128)
        );

    const __m512i max_value =
        _mm512_set1_epi8(
            static_cast<char>(0xFF)
        );

    for (
        ;
        i + 64 <= length;
        i += 64
    ) {

        const __m512i x =
            _mm512_loadu_si512(
                reinterpret_cast<const void*>(
                    data + i
                )
            );

        const __mmask64 mask =
            _mm512_cmpgt_epu8_mask(
                x,
                threshold
            );

        const __m512i result =
            _mm512_maskz_mov_epi8(
                mask,
                max_value
            );

        _mm512_storeu_si512(
            reinterpret_cast<void*>(
                data + i
            ),
            result
        );
    }

#elif defined(__AVX2__)

    // =========================================================================
    // AVX2
    // =========================================================================

    const __m256i sign_bit =
        _mm256_set1_epi8(
            static_cast<char>(0x80)
        );

    const __m256i threshold =
        _mm256_setzero_si256();

    const __m256i max_value =
        _mm256_set1_epi8(
            static_cast<char>(0xFF)
        );

    for (
        ;
        i + 32 <= length;
        i += 32
    ) {

        const __m256i x =
            _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(
                    data + i
                )
            );

        const __m256i flipped =
            _mm256_xor_si256(
                x,
                sign_bit
            );

        const __m256i mask =
            _mm256_cmpgt_epi8(
                flipped,
                threshold
            );

        const __m256i result =
            _mm256_and_si256(
                mask,
                max_value
            );

        _mm256_storeu_si256(
            reinterpret_cast<__m256i*>(
                data + i
            ),
            result
        );
    }

#elif defined(__SSE2__)

    // =========================================================================
    // SSE2
    // =========================================================================

    const __m128i sign_bit =
        _mm_set1_epi8(
            static_cast<char>(0x80)
        );

    const __m128i threshold =
        _mm_setzero_si128();

    const __m128i max_value =
        _mm_set1_epi8(
            static_cast<char>(0xFF)
        );

    for (
        ;
        i + 16 <= length;
        i += 16
    ) {

        const __m128i x =
            _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(
                    data + i
                )
            );

        const __m128i flipped =
            _mm_xor_si128(
                x,
                sign_bit
            );

        const __m128i mask =
            _mm_cmpgt_epi8(
                flipped,
                threshold
            );

        const __m128i result =
            _mm_and_si128(
                mask,
                max_value
            );

        _mm_storeu_si128(
            reinterpret_cast<__m128i*>(
                data + i
            ),
            result
        );
    }

#endif

    // =========================================================================
    // Scalar tail
    // =========================================================================

    for (
        ;
        i < length;
        ++i
    ) {

        data[i] =
            (data[i] > 128u)
                ? 255u
                : 0u;
    }
}

// =============================================================================
// Contrast boost
//
// Exact scalar reference:
//
//     value  = input * 294
//     result = value >> 8
//     result = min(result, 255)
//
// Supports:
//
//   AVX-512BW
//   AVX2
//   SSE4.1
//   SSE2
//   scalar
//
// =============================================================================

inline void contrast_boost_simd(
    std::uint8_t* __restrict__ data,
    std::size_t length
) noexcept {

    if (
        data == nullptr ||
        length == 0
    ) {
        return;
    }

    std::size_t i = 0;

#if defined(__AVX512BW__)

    // =========================================================================
    // AVX-512BW
    // =========================================================================

    const __m512i factor =
        _mm512_set1_epi16(
            38
        );

    const __m512i max_value =
        _mm512_set1_epi16(
            255
        );

    for (
        ;
        i + 64 <= length;
        i += 64
    ) {

        const __m512i raw =
            _mm512_loadu_si512(
                reinterpret_cast<const void*>(
                    data + i
                )
            );

        const __m256i raw_lo =
            _mm512_castsi512_si256(
                raw
            );

        const __m256i raw_hi =
            _mm512_extracti64x4_epi64(
                raw,
                1
            );

        const __m512i lo =
            _mm512_cvtepu8_epi16(
                raw_lo
            );

        const __m512i hi =
            _mm512_cvtepu8_epi16(
                raw_hi
            );

        const __m512i lo_extra =
            _mm512_srli_epi16(
                _mm512_mullo_epi16(
                    lo,
                    factor
                ),
                8
            );

        const __m512i hi_extra =
            _mm512_srli_epi16(
                _mm512_mullo_epi16(
                    hi,
                    factor
                ),
                8
            );

        __m512i lo_result =
            _mm512_add_epi16(
                lo,
                lo_extra
            );

        __m512i hi_result =
            _mm512_add_epi16(
                hi,
                hi_extra
            );

        lo_result =
            _mm512_min_epu16(
                lo_result,
                max_value
            );

        hi_result =
            _mm512_min_epu16(
                hi_result,
                max_value
            );

        const __m256i packed_lo =
            _mm512_cvtusepi16_epi8(
                lo_result
            );

        const __m256i packed_hi =
            _mm512_cvtusepi16_epi8(
                hi_result
            );

        const __m512i result =
            _mm512_castsi256_si512(
                packed_lo
            );

        const __m512i final_result =
            _mm512_inserti64x4(
                result,
                packed_hi,
                1
            );

        _mm512_storeu_si512(
            reinterpret_cast<void*>(
                data + i
            ),
            final_result
        );
    }

#elif defined(__AVX2__)

    // =========================================================================
    // AVX2
    // =========================================================================

    const __m256i factor =
        _mm256_set1_epi16(
            38
        );

    const __m256i max_value =
        _mm256_set1_epi16(
            255
        );

    for (
        ;
        i + 32 <= length;
        i += 32
    ) {

        const __m256i raw =
            _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(
                    data + i
                )
            );

        const __m128i raw_lo =
            _mm256_castsi256_si128(
                raw
            );

        const __m128i raw_hi =
            _mm256_extracti128_si256(
                raw,
                1
            );

        const __m256i lo =
            _mm256_cvtepu8_epi16(
                raw_lo
            );

        const __m256i hi =
            _mm256_cvtepu8_epi16(
                raw_hi
            );

        const __m256i lo_extra =
            _mm256_srli_epi16(
                _mm256_mullo_epi16(
                    lo,
                    factor
                ),
                8
            );

        const __m256i hi_extra =
            _mm256_srli_epi16(
                _mm256_mullo_epi16(
                    hi,
                    factor
                ),
                8
            );

        __m256i out_lo =
            _mm256_add_epi16(
                lo,
                lo_extra
            );

        __m256i out_hi =
            _mm256_add_epi16(
                hi,
                hi_extra
            );

        out_lo =
            _mm256_min_epu16(
                out_lo,
                max_value
            );

        out_hi =
            _mm256_min_epu16(
                out_hi,
                max_value
            );

        const __m256i packed =
            _mm256_packus_epi16(
                out_lo,
                out_hi
            );

        /*
         * _mm256_packus_epi16() works independently on the two
         * 128-bit lanes.
         *
         * Rearrange:
         *
         *   lo[0..7]
         *   hi[0..7]
         *   lo[8..15]
         *   hi[8..15]
         *
         * into:
         *
         *   lo[0..15]
         *   hi[0..15]
         */
        const __m256i result =
            _mm256_permute4x64_epi64(
                packed,
                _MM_SHUFFLE(
                    3,
                    1,
                    2,
                    0
                )
            );

        _mm256_storeu_si256(
            reinterpret_cast<__m256i*>(
                data + i
            ),
            result
        );
    }

#elif defined(__SSE4_1__)

    // =========================================================================
    // SSE4.1
    // =========================================================================

    const __m128i factor =
        _mm_set1_epi16(
            38
        );

    const __m128i max_value =
        _mm_set1_epi16(
            255
        );

    const __m128i zero =
        _mm_setzero_si128();

    for (
        ;
        i + 16 <= length;
        i += 16
    ) {

        const __m128i raw =
            _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(
                    data + i
                )
            );

        const __m128i lo =
            _mm_unpacklo_epi8(
                raw,
                zero
            );

        const __m128i hi =
            _mm_unpackhi_epi8(
                raw,
                zero
            );

        const __m128i lo_extra =
            _mm_srli_epi16(
                _mm_mullo_epi16(
                    lo,
                    factor
                ),
                8
            );

        const __m128i hi_extra =
            _mm_srli_epi16(
                _mm_mullo_epi16(
                    hi,
                    factor
                ),
                8
            );

        __m128i out_lo =
            _mm_add_epi16(
                lo,
                lo_extra
            );

        __m128i out_hi =
            _mm_add_epi16(
                hi,
                hi_extra
            );

        out_lo =
            _mm_min_epu16(
                out_lo,
                max_value
            );

        out_hi =
            _mm_min_epu16(
                out_hi,
                max_value
            );

        const __m128i result =
            _mm_packus_epi16(
                out_lo,
                out_hi
            );

        _mm_storeu_si128(
            reinterpret_cast<__m128i*>(
                data + i
            ),
            result
        );
    }

#elif defined(__SSE2__)

    // =========================================================================
    // SSE2
    // =========================================================================
    //
    // IMPORTANT:
    //
    // _mm_min_epu16() is NOT available here.
    //
    // We use _mm_cmpgt_epi16() instead.
    //
    // The calculated values are:
    //
    //     input <= 255
    //     result <= 292
    //
    // therefore all values fit safely in signed int16_t.
    //
    // This makes signed comparison semantically equivalent to unsigned
    // comparison for this particular operation.
    //
    // =========================================================================

    const __m128i factor =
        _mm_set1_epi16(
            38
        );

    const __m128i max_value =
        _mm_set1_epi16(
            255
        );

    const __m128i zero =
        _mm_setzero_si128();

    for (
        ;
        i + 16 <= length;
        i += 16
    ) {

        const __m128i raw =
            _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(
                    data + i
                )
            );

        const __m128i lo =
            _mm_unpacklo_epi8(
                raw,
                zero
            );

        const __m128i hi =
            _mm_unpackhi_epi8(
                raw,
                zero
            );

        const __m128i lo_extra =
            _mm_srli_epi16(
                _mm_mullo_epi16(
                    lo,
                    factor
                ),
                8
            );

        const __m128i hi_extra =
            _mm_srli_epi16(
                _mm_mullo_epi16(
                    hi,
                    factor
                ),
                8
            );

        __m128i out_lo =
            _mm_add_epi16(
                lo,
                lo_extra
            );

        __m128i out_hi =
            _mm_add_epi16(
                hi,
                hi_extra
            );

        // ---------------------------------------------------------------------
        // SSE2-compatible clamp:
        //
        // out = min(out, 255)
        // ---------------------------------------------------------------------

        const __m128i lo_greater =
            _mm_cmpgt_epi16(
                out_lo,
                max_value
            );

        const __m128i hi_greater =
            _mm_cmpgt_epi16(
                out_hi,
                max_value
            );

        out_lo =
            _mm_or_si128(
                _mm_andnot_si128(
                    lo_greater,
                    out_lo
                ),
                _mm_and_si128(
                    lo_greater,
                    max_value
                )
            );

        out_hi =
            _mm_or_si128(
                _mm_andnot_si128(
                    hi_greater,
                    out_hi
                ),
                _mm_and_si128(
                    hi_greater,
                    max_value
                )
            );

        const __m128i result =
            _mm_packus_epi16(
                out_lo,
                out_hi
            );

        _mm_storeu_si128(
            reinterpret_cast<__m128i*>(
                data + i
            ),
            result
        );
    }

#endif

    // =========================================================================
    // Scalar tail
    // =========================================================================

    for (
        ;
        i < length;
        ++i
    ) {

        const std::uint32_t value =
            static_cast<std::uint32_t>(
                data[i]
            ) *
            294u;

        const std::uint32_t result =
            value >> 8u;

        data[i] =
            static_cast<std::uint8_t>(
                result > 255u
                    ? 255u
                    : result
            );
    }
}

} // namespace fin::ops

#endif // TENSOR_OPS_HPP
