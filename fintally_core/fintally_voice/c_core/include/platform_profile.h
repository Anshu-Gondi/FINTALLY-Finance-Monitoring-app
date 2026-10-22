#ifndef FIN_VOICE_PLATFORM_PROFILE_H
#define FIN_VOICE_PLATFORM_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "macros/cache_alignment.h"
#include "kernel/simd_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portable platform and workload capability profile for FinTally Voice.
 *
 * This module detects compile-target architecture/OS and asks the SIMD
 * dispatcher to choose a compiled, runtime-supported kernel. Portable C cannot
 * reliably discover available RAM, physical cache sizes, or live thermal
 * readings on every target; callers may provide such information as hints.
 *
 * All working-set budgets are software targets, not guarantees about physical
 * CPU-cache residency. The value is always capped at 500 KiB (512000 bytes).
 */
#define FIN_VOICE_WORKING_SET_CAP_BYTES (UINT64_C(500) * UINT64_C(1024))
#define FIN_VOICE_DEFAULT_SAMPLE_RATE_HZ UINT32_C(16000)
#define FIN_VOICE_DEFAULT_FRAME_SAMPLES UINT16_C(320)
#define FIN_VOICE_DEFAULT_HOP_SAMPLES UINT16_C(160)

typedef enum FinVoiceArchitecture {
    FIN_VOICE_ARCH_UNKNOWN = 0,
    FIN_VOICE_ARCH_X86_32,
    FIN_VOICE_ARCH_X86_64,
    FIN_VOICE_ARCH_ARM_32,
    FIN_VOICE_ARCH_ARM_64,
    FIN_VOICE_ARCH_ESP32,
    FIN_VOICE_ARCH_RISCV_32,
    FIN_VOICE_ARCH_RISCV_64,
    FIN_VOICE_ARCH_WASM_32,
    FIN_VOICE_ARCH_WASM_64,
    FIN_VOICE_ARCH_OTHER
} FinVoiceArchitecture;

typedef enum FinVoiceEnvironment {
    FIN_VOICE_ENV_UNKNOWN = 0,
    FIN_VOICE_ENV_WINDOWS,
    FIN_VOICE_ENV_LINUX,
    FIN_VOICE_ENV_ANDROID,
    FIN_VOICE_ENV_APPLE,
    FIN_VOICE_ENV_ESP_IDF,
    FIN_VOICE_ENV_BARE_METAL,
    FIN_VOICE_ENV_WASM,
    FIN_VOICE_ENV_OTHER
} FinVoiceEnvironment;

typedef enum FinVoiceDeviceTier {
    FIN_VOICE_TIER_UNKNOWN = 0,
    FIN_VOICE_TIER_TINY,
    FIN_VOICE_TIER_CONSTRAINED,
    FIN_VOICE_TIER_LOW_END,
    FIN_VOICE_TIER_BALANCED,
    FIN_VOICE_TIER_PERFORMANCE
} FinVoiceDeviceTier;

/*
 * Optional caller-supplied runtime hints.
 * A zero numeric field means "unknown/not supplied".
 * No field is interpreted as a verified hardware measurement.
 */
typedef struct FinVoicePlatformHints {
    size_t logical_core_count;
    uint64_t available_memory_bytes;
    uint64_t reported_l2_cache_bytes;
    uint64_t requested_working_set_bytes;
    bool low_power_mode;
} FinVoicePlatformHints;

typedef struct FinVoicePlatformProfile {
    FIN_VOICE_CACHE_ALIGN FinVoiceArchitecture architecture;
    FinVoiceEnvironment environment;
    FinVoiceDeviceTier device_tier;

    size_t logical_core_count;
    uint64_t available_memory_bytes;
    uint64_t reported_l2_cache_bytes;
    uint64_t working_set_budget_bytes;

    uint32_t audio_sample_rate_hz;
    uint16_t audio_frame_samples;
    uint16_t audio_hop_samples;

    FinVoiceSimdBackend selected_backend;
    bool hardware_simd_selected;
    bool low_power_mode;

    /* Initialized dispatcher context; pass this to SIMD wrapper functions. */
    FinVoiceSimdContext simd;
} FinVoicePlatformProfile;

/* Initialize hints to neutral values (unknown numeric values, no low-power). */
void fin_voice_platform_hints_default(FinVoicePlatformHints *hints);

/*
 * Detect target architecture/environment, initialize runtime SIMD selection,
 * infer a conservative device tier, and calculate a working-set budget.
 * 'hints' may be NULL. Returns false only for invalid output or SIMD setup
 * failure; on failure, 'profile' is zeroed as far as portable C permits.
 */
bool fin_voice_platform_profile_detect(
    FinVoicePlatformProfile *profile,
    const FinVoicePlatformHints *hints
);

/* Clamp a requested byte budget to the supported 500 KiB software cap. */
uint64_t fin_voice_platform_clamp_working_set(uint64_t requested_bytes);

const char *fin_voice_architecture_name(FinVoiceArchitecture architecture);
const char *fin_voice_environment_name(FinVoiceEnvironment environment);
const char *fin_voice_device_tier_name(FinVoiceDeviceTier tier);

#ifdef __cplusplus
}
#endif

#endif /* FIN_VOICE_PLATFORM_PROFILE_H */
