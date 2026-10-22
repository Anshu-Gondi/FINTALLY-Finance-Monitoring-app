#include "kernel/simd_dispatch.h"

#include <stddef.h>

/*
 * Runtime dispatcher for quantized UINT8 kernels.
 *
 * Keep this translation unit at the generic compiler target. ISA-specialized
 * instructions belong only in their dedicated backend translation units.
 * Candidate macros mean "this operations table is linked", never "the CPU
 * supports this ISA"; each table must perform its own safe runtime check.
 */
static const FinVoiceSimdOps *const k_backend_candidates[] = {
#if defined(FIN_VOICE_HAVE_SIMD_AVX512)
    &fin_voice_simd_avx512_ops,
#endif
#if defined(FIN_VOICE_HAVE_SIMD_AVX_VNNI)
    &fin_voice_simd_avx_vnni_ops,
#endif
#if defined(FIN_VOICE_HAVE_SIMD_AVX2_FMA)
    &fin_voice_simd_avx2_fma_ops,
#endif
#if defined(FIN_VOICE_HAVE_SIMD_AVX2)
    &fin_voice_simd_avx2_ops,
#endif
#if defined(FIN_VOICE_HAVE_SIMD_SSE42)
    &fin_voice_simd_sse42_ops,
#endif
#if defined(FIN_VOICE_HAVE_SIMD_NEON)
    &fin_voice_simd_neon_ops,
#endif
#if defined(FIN_VOICE_HAVE_SIMD_ESP_DSP)
    &fin_voice_simd_esp_dsp_ops,
#endif
    &fin_voice_simd_scalar_ops
};

static const size_t k_backend_candidate_count =
    sizeof(k_backend_candidates) / sizeof(k_backend_candidates[0]);

/*
 * Reject partially implemented operations tables at selection time. This
 * avoids choosing a nominally supported backend that cannot execute one of
 * the public operations and then failing in the hot path.
 */
static bool fin_voice_simd_ops_supported(const FinVoiceSimdOps *ops)
{
    if (ops == NULL ||
        ops->is_supported == NULL ||
        ops->dot_u8 == NULL ||
        ops->squared_distance_u8 == NULL ||
        ops->nearest_u8 == NULL) {
        return false;
    }

    return ops->is_supported();
}

static const FinVoiceSimdOps *fin_voice_simd_find_backend(
    FinVoiceSimdBackend backend
)
{
    size_t i;

    for (i = 0U; i < k_backend_candidate_count; ++i) {
        const FinVoiceSimdOps *const candidate = k_backend_candidates[i];

        if (candidate != NULL && candidate->backend == backend) {
            return candidate;
        }
    }

    return NULL;
}

static const FinVoiceSimdOps *fin_voice_simd_find_best_supported(void)
{
    size_t i;

    for (i = 0U; i < k_backend_candidate_count; ++i) {
        const FinVoiceSimdOps *const candidate = k_backend_candidates[i];

        if (fin_voice_simd_ops_supported(candidate)) {
            return candidate;
        }
    }

    /* The scalar table is required and always remains the final candidate. */
    return &fin_voice_simd_scalar_ops;
}

bool fin_voice_simd_init(
    FinVoiceSimdContext *context,
    FinVoiceSimdBackend requested_backend
)
{
    const FinVoiceSimdOps *selected;

    if (context == NULL) {
        return false;
    }

    /* Leave a valid fallback installed even on explicit-selection failure. */
    context->ops = &fin_voice_simd_scalar_ops;

    if (requested_backend == FIN_SIMD_AUTO) {
        selected = fin_voice_simd_find_best_supported();

        if (!fin_voice_simd_ops_supported(selected)) {
            selected = &fin_voice_simd_scalar_ops;
        }

        context->ops = selected;
        return true;
    }

    selected = fin_voice_simd_find_backend(requested_backend);

    if (!fin_voice_simd_ops_supported(selected)) {
        return false;
    }

    context->ops = selected;
    return true;
}

FinVoiceSimdBackend fin_voice_simd_context_backend(
    const FinVoiceSimdContext *context
)
{
    if (context == NULL || context->ops == NULL) {
        return FIN_SIMD_AUTO;
    }

    return context->ops->backend;
}

const char *fin_voice_simd_backend_name(FinVoiceSimdBackend backend)
{
    /* Names do not depend on whether a backend is linked into this build. */
    switch (backend) {
        case FIN_SIMD_AUTO:
            return "auto";
        case FIN_SIMD_SCALAR:
            return "scalar";
        case FIN_SIMD_SSE42:
            return "sse4.2";
        case FIN_SIMD_AVX2:
            return "avx2";
        case FIN_SIMD_AVX2_VNNI:
            return "avx-vnni";
        case FIN_SIMD_AVX512:
            return "avx512bw";
        case FIN_SIMD_NEON:
            return "neon";
        case FIN_SIMD_ESP_DSP:
            return "esp-dsp";
        case FIN_SIMD_AVX2_FMA:
            return "avx2+fma";
        default:
            return "unknown";
    }
}

bool fin_voice_simd_backend_available(FinVoiceSimdBackend backend)
{
    if (backend == FIN_SIMD_AUTO) {
        /* AUTO is available because the scalar fallback is required. */
        return true;
    }

    return fin_voice_simd_ops_supported(
        fin_voice_simd_find_backend(backend));
}

bool fin_voice_simd_dot_u8(
    const FinVoiceSimdContext *context,
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    uint8_t zero_point,
    float *out_f32
)
{
    if (context == NULL || context->ops == NULL ||
        context->ops->dot_u8 == NULL) {
        return false;
    }

    return context->ops->dot_u8(
        lhs, rhs, count, scale, zero_point, out_f32);
}

bool fin_voice_simd_squared_distance_u8(
    const FinVoiceSimdContext *context,
    const uint8_t *lhs,
    const uint8_t *rhs,
    size_t count,
    float scale,
    float *out_squared_distance_f32
)
{
    if (context == NULL || context->ops == NULL ||
        context->ops->squared_distance_u8 == NULL) {
        return false;
    }

    return context->ops->squared_distance_u8(
        lhs, rhs, count, scale, out_squared_distance_f32);
}

bool fin_voice_simd_nearest_u8(
    const FinVoiceSimdContext *context,
    const uint8_t *query,
    const uint8_t *centroids,
    size_t centroid_count,
    size_t dimension,
    float scale,
    size_t *out_index,
    float *out_squared_distance_f32
)
{
    if (context == NULL || context->ops == NULL ||
        context->ops->nearest_u8 == NULL) {
        return false;
    }

    return context->ops->nearest_u8(
        query,
        centroids,
        centroid_count,
        dimension,
        scale,
        out_index,
        out_squared_distance_f32);
}
