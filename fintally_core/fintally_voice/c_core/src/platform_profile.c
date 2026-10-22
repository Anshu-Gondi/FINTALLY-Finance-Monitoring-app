#include "platform_profile.h"

#include <string.h>

/* ------------------------------------------------------------------------- */
/* Compile-target detection                                                  */
/* ------------------------------------------------------------------------- */

static FinVoiceArchitecture fin_voice_detect_architecture(void)
{
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32) || defined(__ESP32__)
    return FIN_VOICE_ARCH_ESP32;
#elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    return FIN_VOICE_ARCH_X86_64;
#elif defined(__i386__) || defined(_M_IX86)
    return FIN_VOICE_ARCH_X86_32;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return FIN_VOICE_ARCH_ARM_64;
#elif defined(__arm__) || defined(_M_ARM)
    return FIN_VOICE_ARCH_ARM_32;
#elif defined(__wasm64__)
    return FIN_VOICE_ARCH_WASM_64;
#elif defined(__wasm32__)
    return FIN_VOICE_ARCH_WASM_32;
#elif defined(__riscv) && (__riscv_xlen == 64)
    return FIN_VOICE_ARCH_RISCV_64;
#elif defined(__riscv) && (__riscv_xlen == 32)
    return FIN_VOICE_ARCH_RISCV_32;
#elif defined(__riscv)
    return FIN_VOICE_ARCH_OTHER;
#else
    return FIN_VOICE_ARCH_UNKNOWN;
#endif
}

static FinVoiceEnvironment fin_voice_detect_environment(void)
{
#if defined(ESP_PLATFORM)
    return FIN_VOICE_ENV_ESP_IDF;
#elif defined(__ANDROID__)
    return FIN_VOICE_ENV_ANDROID;
#elif defined(_WIN32)
    return FIN_VOICE_ENV_WINDOWS;
#elif defined(__APPLE__) && defined(__MACH__)
    return FIN_VOICE_ENV_APPLE;
#elif defined(__linux__)
    return FIN_VOICE_ENV_LINUX;
#elif defined(__wasm32__) || defined(__wasm64__)
    return FIN_VOICE_ENV_WASM;
#elif defined(FIN_VOICE_BARE_METAL)
    return FIN_VOICE_ENV_BARE_METAL;
#elif defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0)
    return FIN_VOICE_ENV_BARE_METAL;
#else
    return FIN_VOICE_ENV_OTHER;
#endif
}

/* ------------------------------------------------------------------------- */
/* Conservative tier and budget inference                                    */
/* ------------------------------------------------------------------------- */

static FinVoiceDeviceTier fin_voice_infer_tier(
    FinVoiceArchitecture architecture,
    size_t logical_core_count,
    uint64_t available_memory_bytes,
    bool low_power_mode
)
{
    FinVoiceDeviceTier tier;

    if (architecture == FIN_VOICE_ARCH_ESP32) {
        tier = FIN_VOICE_TIER_TINY;
    } else if (architecture == FIN_VOICE_ARCH_ARM_32 ||
               architecture == FIN_VOICE_ARCH_RISCV_32 ||
               architecture == FIN_VOICE_ARCH_WASM_32) {
        tier = FIN_VOICE_TIER_CONSTRAINED;
    } else if (logical_core_count != 0U && logical_core_count <= 2U) {
        tier = FIN_VOICE_TIER_LOW_END;
    } else if (available_memory_bytes != 0U &&
               available_memory_bytes <= (UINT64_C(1) << 30U)) {
        tier = FIN_VOICE_TIER_LOW_END;
    } else if ((logical_core_count != 0U && logical_core_count <= 4U) ||
               (available_memory_bytes != 0U &&
                available_memory_bytes <= (UINT64_C(4) << 30U))) {
        tier = FIN_VOICE_TIER_BALANCED;
    } else if (logical_core_count > 4U || available_memory_bytes > 0U) {
        tier = FIN_VOICE_TIER_PERFORMANCE;
    } else if (architecture == FIN_VOICE_ARCH_X86_64 ||
               architecture == FIN_VOICE_ARCH_ARM_64 ||
               architecture == FIN_VOICE_ARCH_RISCV_64) {
        /* Unknown resources: assume balanced, not high-performance. */
        tier = FIN_VOICE_TIER_BALANCED;
    } else {
        tier = FIN_VOICE_TIER_UNKNOWN;
    }

    if (low_power_mode) {
        switch (tier) {
        case FIN_VOICE_TIER_PERFORMANCE:
            tier = FIN_VOICE_TIER_BALANCED;
            break;
        case FIN_VOICE_TIER_BALANCED:
            tier = FIN_VOICE_TIER_LOW_END;
            break;
        case FIN_VOICE_TIER_LOW_END:
            tier = FIN_VOICE_TIER_CONSTRAINED;
            break;
        case FIN_VOICE_TIER_CONSTRAINED:
            tier = FIN_VOICE_TIER_TINY;
            break;
        case FIN_VOICE_TIER_TINY:
        case FIN_VOICE_TIER_UNKNOWN:
        default:
            break;
        }
    }

    return tier;
}

static uint64_t fin_voice_default_budget_for_tier(FinVoiceDeviceTier tier)
{
    switch (tier) {
    case FIN_VOICE_TIER_TINY:
        return UINT64_C(24) * UINT64_C(1024);
    case FIN_VOICE_TIER_CONSTRAINED:
        return UINT64_C(64) * UINT64_C(1024);
    case FIN_VOICE_TIER_LOW_END:
        return UINT64_C(128) * UINT64_C(1024);
    case FIN_VOICE_TIER_BALANCED:
        return UINT64_C(256) * UINT64_C(1024);
    case FIN_VOICE_TIER_PERFORMANCE:
        return FIN_VOICE_WORKING_SET_CAP_BYTES;
    case FIN_VOICE_TIER_UNKNOWN:
    default:
        return UINT64_C(128) * UINT64_C(1024);
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void fin_voice_platform_hints_default(FinVoicePlatformHints *hints)
{
    if (hints == NULL) {
        return;
    }

    hints->logical_core_count = 0U;
    hints->available_memory_bytes = UINT64_C(0);
    hints->reported_l2_cache_bytes = UINT64_C(0);
    hints->requested_working_set_bytes = UINT64_C(0);
    hints->low_power_mode = false;
}

uint64_t fin_voice_platform_clamp_working_set(uint64_t requested_bytes)
{
    if (requested_bytes > FIN_VOICE_WORKING_SET_CAP_BYTES) {
        return FIN_VOICE_WORKING_SET_CAP_BYTES;
    }

    return requested_bytes;
}

bool fin_voice_platform_profile_detect(
    FinVoicePlatformProfile *profile,
    const FinVoicePlatformHints *hints
)
{
    FinVoicePlatformHints defaults;
    const FinVoicePlatformHints *effective_hints = hints;
    uint64_t requested_budget;

    if (profile == NULL) {
        return false;
    }

    (void)memset(profile, 0, sizeof(*profile));

    if (effective_hints == NULL) {
        fin_voice_platform_hints_default(&defaults);
        effective_hints = &defaults;
    }

    profile->architecture = fin_voice_detect_architecture();
    profile->environment = fin_voice_detect_environment();
    profile->logical_core_count = effective_hints->logical_core_count;
    profile->available_memory_bytes = effective_hints->available_memory_bytes;
    profile->reported_l2_cache_bytes = effective_hints->reported_l2_cache_bytes;
    profile->low_power_mode = effective_hints->low_power_mode;

    profile->device_tier = fin_voice_infer_tier(
        profile->architecture,
        profile->logical_core_count,
        profile->available_memory_bytes,
        profile->low_power_mode
    );

    if (!fin_voice_simd_init(&profile->simd, FIN_VOICE_SIMD_AUTO)) {
        (void)memset(profile, 0, sizeof(*profile));
        return false;
    }

    profile->selected_backend =
        fin_voice_simd_context_backend(&profile->simd);
    profile->hardware_simd_selected =
        profile->selected_backend != FIN_VOICE_SIMD_SCALAR;

    profile->audio_sample_rate_hz = FIN_VOICE_DEFAULT_SAMPLE_RATE_HZ;
    profile->audio_frame_samples = FIN_VOICE_DEFAULT_FRAME_SAMPLES;
    profile->audio_hop_samples = FIN_VOICE_DEFAULT_HOP_SAMPLES;

    requested_budget = effective_hints->requested_working_set_bytes;
    if (requested_budget == UINT64_C(0)) {
        requested_budget = fin_voice_default_budget_for_tier(profile->device_tier);
    }

    profile->working_set_budget_bytes =
        fin_voice_platform_clamp_working_set(requested_budget);

    return true;
}

const char *fin_voice_architecture_name(FinVoiceArchitecture architecture)
{
    switch (architecture) {
    case FIN_VOICE_ARCH_X86_32: return "x86-32";
    case FIN_VOICE_ARCH_X86_64: return "x86-64";
    case FIN_VOICE_ARCH_ARM_32: return "ARM-32";
    case FIN_VOICE_ARCH_ARM_64: return "ARM-64";
    case FIN_VOICE_ARCH_ESP32: return "ESP32";
    case FIN_VOICE_ARCH_RISCV_32: return "RISC-V-32";
    case FIN_VOICE_ARCH_RISCV_64: return "RISC-V-64";
    case FIN_VOICE_ARCH_WASM_32: return "WASM-32";
    case FIN_VOICE_ARCH_WASM_64: return "WASM-64";
    case FIN_VOICE_ARCH_OTHER: return "other";
    case FIN_VOICE_ARCH_UNKNOWN:
    default: return "unknown";
    }
}

const char *fin_voice_environment_name(FinVoiceEnvironment environment)
{
    switch (environment) {
    case FIN_VOICE_ENV_WINDOWS: return "Windows";
    case FIN_VOICE_ENV_LINUX: return "Linux";
    case FIN_VOICE_ENV_ANDROID: return "Android";
    case FIN_VOICE_ENV_APPLE: return "Apple";
    case FIN_VOICE_ENV_ESP_IDF: return "ESP-IDF";
    case FIN_VOICE_ENV_BARE_METAL: return "bare-metal";
    case FIN_VOICE_ENV_WASM: return "WebAssembly";
    case FIN_VOICE_ENV_OTHER: return "other";
    case FIN_VOICE_ENV_UNKNOWN:
    default: return "unknown";
    }
}

const char *fin_voice_device_tier_name(FinVoiceDeviceTier tier)
{
    switch (tier) {
    case FIN_VOICE_TIER_TINY: return "tiny";
    case FIN_VOICE_TIER_CONSTRAINED: return "constrained";
    case FIN_VOICE_TIER_LOW_END: return "low-end";
    case FIN_VOICE_TIER_BALANCED: return "balanced";
    case FIN_VOICE_TIER_PERFORMANCE: return "performance";
    case FIN_VOICE_TIER_UNKNOWN:
    default: return "unknown";
    }
}
