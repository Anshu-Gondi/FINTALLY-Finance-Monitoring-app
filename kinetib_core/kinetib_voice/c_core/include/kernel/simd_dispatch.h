#ifndef FIN_VOICE_SIMD_DISPATCH_H
#define FIN_VOICE_SIMD_DISPATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "macros/cache_alignment.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Single public backend enum shared by acoustic_dsp.h, platform_profile.h,
 * Rust FFI declarations, and the implementation-specific operations tables.
 *
 * Numeric values of the original FIN_SIMD_* entries are preserved:
 * AUTO=0, SCALAR=1, SSE4.2=2, AVX2=3, AVX2-VNNI=4, AVX-512=5, NEON=6.
 * New backends are appended rather than shifting those values.
 */
typedef enum FinVoiceSimdBackend {
    FIN_SIMD_AUTO = 0,
    FIN_SIMD_SCALAR = 1,
    FIN_SIMD_SSE42 = 2,
    FIN_SIMD_AVX2 = 3,
    FIN_SIMD_AVX2_VNNI = 4,
    FIN_SIMD_AVX512 = 5,
    FIN_SIMD_NEON = 6,
    FIN_SIMD_ESP_DSP = 7,
    FIN_SIMD_AVX2_FMA = 8,

    /* Names used by backend-specific translation units (aliases). */
    FIN_VOICE_SIMD_AUTO = FIN_SIMD_AUTO,
    FIN_VOICE_SIMD_SCALAR = FIN_SIMD_SCALAR,
    FIN_VOICE_SIMD_SSE42 = FIN_SIMD_SSE42,
    FIN_VOICE_SIMD_AVX2 = FIN_SIMD_AVX2,
    FIN_VOICE_SIMD_AVX2_FMA = FIN_SIMD_AVX2_FMA,
    FIN_VOICE_SIMD_AVX_VNNI = FIN_SIMD_AVX2_VNNI,
    FIN_VOICE_SIMD_AVX512 = FIN_SIMD_AVX512,
    FIN_VOICE_SIMD_NEON = FIN_SIMD_NEON,
    FIN_VOICE_SIMD_ESP_DSP = FIN_SIMD_ESP_DSP
} FinVoiceSimdBackend;

typedef bool (*FinVoiceSimdSupportFn)(void);

typedef bool (*FinVoiceSimdDotU8Fn)(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    uint8_t zero_point,
    float *out_f32
);

typedef bool (*FinVoiceSimdSquaredDistanceU8Fn)(
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    float *out_squared_distance_f32
);

typedef bool (*FinVoiceSimdNearestU8Fn)(
    const uint8_t *query,
    const uint8_t *centroids,
    size_t centroid_count,
    size_t dimension,
    float scale,
    size_t *out_index,
    float *out_squared_distance_f32
);

typedef struct FinVoiceSimdOps {
    /* The table is static/read-only and starts on its own cache line. */
    FIN_VOICE_CACHE_ALIGN FinVoiceSimdBackend backend;
    const char *name;
    FinVoiceSimdSupportFn is_supported;
    FinVoiceSimdDotU8Fn dot_u8;
    FinVoiceSimdSquaredDistanceU8Fn squared_distance_u8;
    FinVoiceSimdNearestU8Fn nearest_u8;
} FinVoiceSimdOps;

/* Small dispatch handle; it owns no memory and is safe to copy after init. */
typedef struct FinVoiceSimdContext {
    FIN_VOICE_CACHE_ALIGN const FinVoiceSimdOps *ops;
} FinVoiceSimdContext;

extern const FinVoiceSimdOps fin_voice_simd_scalar_ops;
extern const FinVoiceSimdOps fin_voice_simd_sse42_ops;
extern const FinVoiceSimdOps fin_voice_simd_neon_ops;
extern const FinVoiceSimdOps fin_voice_simd_avx2_ops;
extern const FinVoiceSimdOps fin_voice_simd_avx2_fma_ops;
extern const FinVoiceSimdOps fin_voice_simd_avx_vnni_ops;
extern const FinVoiceSimdOps fin_voice_simd_avx512_ops;
extern const FinVoiceSimdOps fin_voice_simd_esp_dsp_ops;

/*
 * AUTO chooses the best compiled and runtime-supported backend.
 * If an explicitly requested backend is unavailable, init leaves the context
 * on scalar and returns false. The scalar backend is always required.
 */
bool fin_voice_simd_init(
    FinVoiceSimdContext *context,
    FinVoiceSimdBackend requested_backend
);

FinVoiceSimdBackend fin_voice_simd_context_backend(
    const FinVoiceSimdContext *context
);

const char *fin_voice_simd_backend_name(FinVoiceSimdBackend backend);
bool fin_voice_simd_backend_available(FinVoiceSimdBackend backend);

/* UINT8 integer-domain processing, converted to FP32 output. */
bool fin_voice_simd_dot_u8(
    const FinVoiceSimdContext *context,
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    uint8_t zero_point,
    float *out_f32
);

bool fin_voice_simd_squared_distance_u8(
    const FinVoiceSimdContext *context,
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    float *out_squared_distance_f32
);

bool fin_voice_simd_nearest_u8(
    const FinVoiceSimdContext *context,
    const uint8_t *query,
    const uint8_t *centroids,
    size_t centroid_count,
    size_t dimension,
    float scale,
    size_t *out_index,
    float *out_squared_distance_f32
);

#ifdef __cplusplus
}
#endif

#endif /* FIN_VOICE_SIMD_DISPATCH_H */
