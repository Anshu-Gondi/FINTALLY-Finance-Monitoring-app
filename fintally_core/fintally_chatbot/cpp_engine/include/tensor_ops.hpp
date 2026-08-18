#ifndef TENSOR_OPS_HPP
#define TENSOR_OPS_HPP

#include <cstdint>
#include <cstddef>
#include <cstdlib>

#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <immintrin.h>
#endif

namespace fin {
namespace ops {

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

// Optimized Multi-ISA Binarization with Aligned Memory & Stream Store Support
inline void binarize_simd(uint8_t* __restrict__ data, size_t length) {
    size_t i = 0;

#if defined(__GNUC__) || defined(__clang__)
    uint8_t* ptr = static_cast<uint8_t*>(__builtin_assume_aligned(data, 64));
#else
    uint8_t* ptr = data;
#endif

#if defined(__AVX512BW__)
    const __m512i thresh512 = _mm512_set1_epi8(static_cast<char>(128));
    const __m512i max_val512 = _mm512_set1_epi8(static_cast<char>(255));
    const __m512i zero512 = _mm512_setzero_si512();

    for (; i + 63 < length; i += 64) {
        __m512i chunk = _mm512_load_si512(reinterpret_cast<const __m512i*>(&ptr[i]));
        __mmask64 mask = _mm512_cmpgt_epu8_mask(chunk, thresh512);
        __m512i result = _mm512_mask_blend_epi8(mask, zero512, max_val512);

        if (length >= 2 * 1024 * 1024) {
            _mm512_stream_si512(reinterpret_cast<__m512i*>(&ptr[i]), result);
        } else {
            _mm512_store_si512(reinterpret_cast<__m512i*>(&ptr[i]), result);
        }
    }
#elif defined(__AVX2__)
    const __m256i sign_flip = _mm256_set1_epi8(static_cast<char>(0x80));
    const __m256i thresh256 = _mm256_set1_epi8(static_cast<char>(128));
    const __m256i thresh_flipped = _mm256_xor_si256(thresh256, sign_flip);

    for (; i + 31 < length; i += 32) {
        __m256i chunk = _mm256_load_si256(reinterpret_cast<const __m256i*>(&ptr[i]));
        __m256i chunk_flipped = _mm256_xor_si256(chunk, sign_flip);
        __m256i mask = _mm256_cmpgt_epi8(chunk_flipped, thresh_flipped);

        if (length >= 2 * 1024 * 1024) {
            _mm256_stream_si256(reinterpret_cast<__m256i*>(&ptr[i]), mask);
        } else {
            _mm256_store_si256(reinterpret_cast<__m256i*>(&ptr[i]), mask);
        }
    }
#elif defined(__SSE4_2__) || defined(__SSSE3__)
    const __m128i sign_flip = _mm_set1_epi8(static_cast<char>(0x80));
    const __m128i thresh128 = _mm_set1_epi8(static_cast<char>(128));
    const __m128i thresh_flipped = _mm_xor_si128(thresh128, sign_flip);

    for (; i + 15 < length; i += 16) {
        __m128i chunk = _mm_load_si128(reinterpret_cast<const __m128i*>(&ptr[i]));
        __m128i chunk_flipped = _mm_xor_si128(chunk, sign_flip);
        __m128i mask = _mm_cmpgt_epi8(chunk_flipped, thresh_flipped);

        _mm_store_si128(reinterpret_cast<__m128i*>(&ptr[i]), mask);
    }
#endif

    // Scalar tail processing
    for (; i < length; ++i) {
        ptr[i] = (ptr[i] > 128) ? 255 : 0;
    }

#if defined(__AVX2__) || defined(__AVX512F__)
    if (length >= 2 * 1024 * 1024) {
        _mm_sfence();
    }
#endif
}

// Fast Contrast Boost
inline void contrast_boost_simd(uint8_t* __restrict__ data, size_t length) {
    size_t i = 0;

#if defined(__GNUC__) || defined(__clang__)
    uint8_t* ptr = static_cast<uint8_t*>(__builtin_assume_aligned(data, 64));
#else
    uint8_t* ptr = data;
#endif

#if defined(__AVX2__)
    const __m256i c38 = _mm256_set1_epi16(38);

    for (; i + 31 < length; i += 32) {
        __m256i raw = _mm256_load_si256(reinterpret_cast<const __m256i*>(&ptr[i]));

        __m256i lo16 = _mm256_cvtepu8_epi16(_mm256_castsi256_si128(raw));
        __m256i hi16 = _mm256_cvtepu8_epi16(_mm256_extracti128_si256(raw, 1));

        __m256i prod_lo = _mm256_mullo_epi16(lo16, c38);
        __m256i prod_hi = _mm256_mullo_epi16(hi16, c38);

        __m256i val_lo = _mm256_add_epi16(lo16, _mm256_srli_epi16(prod_lo, 8));
        __m256i val_hi = _mm256_add_epi16(hi16, _mm256_srli_epi16(prod_hi, 8));

        __m256i packed = _mm256_packus_epi16(val_lo, val_hi);
        __m256i result = _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));

        if (length >= 2 * 1024 * 1024) {
            _mm256_stream_si256(reinterpret_cast<__m256i*>(&ptr[i]), result);
        } else {
            _mm256_store_si256(reinterpret_cast<__m256i*>(&ptr[i]), result);
        }
    }
#elif defined(__SSE4_2__) || defined(__SSSE3__)
    const __m128i c38 = _mm_set1_epi16(38);

    for (; i + 15 < length; i += 16) {
        __m128i raw = _mm_load_si128(reinterpret_cast<const __m128i*>(&ptr[i]));

        __m128i lo16 = _mm_cvtepu8_epi16(raw);
        __m128i hi16 = _mm_cvtepu8_epi16(_mm_srli_si128(raw, 8));

        __m128i val_lo = _mm_add_epi16(lo16, _mm_srli_epi16(_mm_mullo_epi16(lo16, c38), 8));
        __m128i val_hi = _mm_add_epi16(hi16, _mm_srli_epi16(_mm_mullo_epi16(hi16, c38), 8));

        __m128i result = _mm_packus_epi16(val_lo, val_hi);
        _mm_store_si128(reinterpret_cast<__m128i*>(&ptr[i]), result);
    }
#endif

    for (; i < length; ++i) {
        uint32_t val = static_cast<uint32_t>(ptr[i]) * 294;
        ptr[i] = (val > 65280) ? 255 : static_cast<uint8_t>(val >> 8);
    }

#if defined(__AVX2__) || defined(__AVX512F__)
    if (length >= 2 * 1024 * 1024) {
        _mm_sfence();
    }
#endif
}

} // namespace ops
} // namespace fin

#endif // TENSOR_OPS_HPP
