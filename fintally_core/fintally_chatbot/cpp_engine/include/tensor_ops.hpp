#ifndef TENSOR_OPS_HPP
#define TENSOR_OPS_HPP

#include <cstdint>
#include <cstddef>
#include <cstdlib>

// Unified x86 intrinsic inclusions for SSE4.2, AVX2, and AVX-512
#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <immintrin.h>
#endif

namespace fin {
namespace ops {

// ---------------------------------------------------------
// Aligned Memory Management for SIMD (64-byte AVX-512 boundary)
// ---------------------------------------------------------
inline void* aligned_alloc(size_t alignment, size_t size) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
#endif
}

inline void aligned_free(void* ptr) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// ---------------------------------------------------------
// Multi-ISA High-Speed Binarization (Scanned PDFs)
// ---------------------------------------------------------
inline void binarize_simd(uint8_t* __restrict__ data, size_t length) {
    size_t i = 0;

#if defined(__AVX512BW__) || defined(__AVX512F__)
    // Process 64 bytes (pixels) per cycle using AVX-512
    const __m512i thresh512 = _mm512_set1_epi8(static_cast<char>(128));
    const __m512i max_val512 = _mm512_set1_epi8(static_cast<char>(255));
    const __m512i zero512 = _mm512_setzero_si512();

    for (; i + 63 < length; i += 64) {
        __m512i chunk = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&data[i]));
        __mmask64 mask = _mm512_cmpgt_epu8_mask(chunk, thresh512);
        __m512i result = _mm512_mask_blend_epi8(mask, zero512, max_val512);
        _mm512_storeu_si512(reinterpret_cast<__m512i*>(&data[i]), result);
    }
#elif defined(__AVX2__)
    // Process 32 bytes per cycle using AVX2 uint8 saturation trick
    const __m256i thresh256 = _mm256_set1_epi8(static_cast<char>(128));
    const __m256i zero256 = _mm256_setzero_si256();

    for (; i + 31 < length; i += 32) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&data[i]));
        // Unsigned subtraction converts >128 values into non-zero positive signed range
        __m256i subs = _mm256_subs_epu8(chunk, thresh256);
        // Compare > 0 directly generates 0xFF mask for pixels > 128
        __m256i mask = _mm256_cmpgt_epi8(subs, zero256);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&data[i]), mask);
    }
#elif defined(__SSE4_2__) || defined(__SSSE3__)
    // Process 16 bytes per cycle using SSE4.2
    const __m128i thresh128 = _mm_set1_epi8(static_cast<char>(128));
    const __m128i zero128 = _mm_setzero_si128();

    for (; i + 15 < length; i += 16) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&data[i]));
        __m128i subs = _mm_subs_epu8(chunk, thresh128);
        __m128i mask = _mm_cmpgt_epi8(subs, zero128);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&data[i]), mask);
    }
#endif

    // Scalar tail processing
    for (; i < length; ++i) {
        data[i] = (data[i] > 128) ? 255 : 0;
    }
}

// ---------------------------------------------------------
// Multi-ISA Fast Contrast Boost (Financial Charts)
// Optimized Math: data[i] * 1.1484375 ≈ data[i] + ((data[i] * 38) >> 8)
// ---------------------------------------------------------
inline void contrast_boost_simd(uint8_t* __restrict__ data, size_t length) {
    size_t i = 0;

#if defined(__AVX2__)
    const __m256i c38 = _mm256_set1_epi16(38);

    for (; i + 31 < length; i += 32) {
        __m256i raw = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&data[i]));

        // Zero-extend 8-bit to 16-bit to prevent multiplication overflow
        __m256i lo16 = _mm256_cvtepu8_epi16(_mm256_castsi256_si128(raw));
        __m256i hi16 = _mm256_cvtepu8_epi16(_mm256_extracti128_si256(raw, 1));

        // Multiply by 38, shift right by 8, add original value
        __m256i prod_lo = _mm256_mullo_epi16(lo16, c38);
        __m256i prod_hi = _mm256_mullo_epi16(hi16, c38);

        __m256i val_lo = _mm256_add_epi16(lo16, _mm256_srli_epi16(prod_lo, 8));
        __m256i val_hi = _mm256_add_epi16(hi16, _mm256_srli_epi16(prod_hi, 8));

        // Saturating pack from 16-bit to 8-bit (automatically clamps > 255 to 255)
        __m256i packed = _mm256_packus_epi16(val_lo, val_hi);

        // Re-order lanes caused by cross-lane packing
        __m256i result = _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&data[i]), result);
    }
#elif defined(__SSE4_2__) || defined(__SSSE3__)
    const __m128i c38 = _mm_set1_epi16(38);

    for (; i + 15 < length; i += 16) {
        __m128i raw = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&data[i]));

        __m128i lo16 = _mm_cvtepu8_epi16(raw);
        __m128i hi16 = _mm_cvtepu8_epi16(_mm_srli_si128(raw, 8));

        __m128i val_lo = _mm_add_epi16(lo16, _mm_srli_epi16(_mm_mullo_epi16(lo16, c38), 8));
        __m128i val_hi = _mm_add_epi16(hi16, _mm_srli_epi16(_mm_mullo_epi16(hi16, c38), 8));

        __m128i result = _mm_packus_epi16(val_lo, val_hi);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&data[i]), result);
    }
#endif

    // Scalar fallback/tail processing
    for (; i < length; ++i) {
        uint32_t val = static_cast<uint32_t>(data[i]) * 294;
        data[i] = (val > 65280) ? 255 : static_cast<uint8_t>(val >> 8);
    }
}

} // namespace ops
} // namespace fin

#endif // TENSOR_OPS_HPP
