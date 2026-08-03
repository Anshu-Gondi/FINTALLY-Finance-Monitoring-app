#ifndef TENSOR_OPS_HPP
#define TENSOR_OPS_HPP

#include <cstdint>
#include <cstddef>
#include <cstdlib>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace fin {
namespace ops {

// ---------------------------------------------------------
// Aligned Memory Management for SIMD (32-byte boundary)
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
// High-Speed Binarization (Scanned PDFs)
// ---------------------------------------------------------
inline void binarize_simd(uint8_t* __restrict__ data, size_t length) {
    size_t i = 0;

#if defined(__AVX2__)
    // Process 32 bytes (pixels) per CPU cycle
    __m256i threshold = _mm256_set1_epi8(char(128));
    __m256i zero      = _mm256_setzero_si256();
    __m256i max_val   = _mm256_set1_epi8(char(255));

    for (; i + 31 < length; i += 32) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&data[i]));
        // Mask where chunk > 128 (using unsigned comparison trick)
        __m256i mask = _mm256_cmpgt_epi8(chunk, threshold);
        // Set to 255 if > 128, else 0
        __m256i result = _mm256_blendv_epi8(zero, max_val, mask);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&data[i]), result);
    }
#endif

    // Scalar fallback/tail processing
    for (; i < length; ++i) {
        data[i] = (data[i] > 128) ? 255 : 0;
    }
}

// ---------------------------------------------------------
// Fast Contrast Boost (Financial Charts)
// ---------------------------------------------------------
inline void contrast_boost_simd(uint8_t* __restrict__ data, size_t length) {
    // Optimized integer math to avoid slow floating-point operations (* 1.15 -> * 294 / 256)
    for (size_t i = 0; i < length; ++i) {
        uint32_t val = data[i] * 294; // 1.15 * 256 ≈ 294
        data[i] = (val > 65280) ? 255 : static_cast<uint8_t>(val >> 8);
    }
}

} // namespace ops
} // namespace fin

#endif // TENSOR_OPS_HPP
