/*
 * FinTally Voice platform-profile regression tests (C23).
 *
 * Intent
 * ------
 * This file tests the public platform-profile contract rather than reaching
 * into its private inference helpers. Resource hints are inputs, not trusted
 * hardware measurements; only architecture/environment are compile-target
 * detections. Cache alignment assertions protect the public type layout used
 * by C callers and any future FFI bindings.
 *
 * The same test is intended to run in Release and ASan/UBSan builds. It does
 * not assume a particular host ISA: it verifies that dispatch selects a
 * backend the dispatcher itself reports as available.
 */
#include "platform_profile.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_GIB_BYTES (UINT64_C(1) << 30U)
#define TEST_KIB_BYTES UINT64_C(1024)

/* The architecture member is deliberately cache-line aligned in the public
 * declaration. The embedded SIMD context has its own alignment requirement. */
_Static_assert(
    _Alignof(FinVoicePlatformProfile) >= FIN_VOICE_CACHE_LINE_BYTES,
    "FinVoicePlatformProfile must preserve its 64-byte alignment"
);
_Static_assert(
    offsetof(FinVoicePlatformProfile, architecture) %
        FIN_VOICE_CACHE_LINE_BYTES == 0U,
    "The architecture member must start on a cache-line boundary"
);
_Static_assert(
    _Alignof(FinVoiceSimdContext) >= FIN_VOICE_CACHE_LINE_BYTES,
    "FinVoiceSimdContext must preserve its declared alignment"
);
_Static_assert(
    offsetof(FinVoicePlatformProfile, simd) %
        _Alignof(FinVoiceSimdContext) == 0U,
    "The embedded SIMD context must be naturally aligned"
);
_Static_assert(
    sizeof(FinVoicePlatformProfile) % _Alignof(FinVoicePlatformProfile) == 0U,
    "Arrays of platform profiles must preserve each element's alignment"
);

static unsigned long g_checks;
static unsigned long g_failures;

#define CHECK(condition) do { \
    ++g_checks; \
    if (!(condition)) { \
        ++g_failures; \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", \
                      __FILE__, __LINE__, #condition); \
    } \
} while (0)

static int is_fixed_tiny_architecture(FinVoiceArchitecture architecture)
{
    return architecture == FIN_VOICE_ARCH_ESP32;
}

static int is_fixed_constrained_architecture(
    FinVoiceArchitecture architecture
)
{
    return architecture == FIN_VOICE_ARCH_ARM_32 ||
           architecture == FIN_VOICE_ARCH_RISCV_32 ||
           architecture == FIN_VOICE_ARCH_WASM_32;
}

static int is_known_64_bit_architecture(FinVoiceArchitecture architecture)
{
    return architecture == FIN_VOICE_ARCH_X86_64 ||
           architecture == FIN_VOICE_ARCH_ARM_64 ||
           architecture == FIN_VOICE_ARCH_RISCV_64;
}

static FinVoiceArchitecture expected_compile_architecture(void)
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
#elif defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 64)
    return FIN_VOICE_ARCH_RISCV_64;
#elif defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 32)
    return FIN_VOICE_ARCH_RISCV_32;
#elif defined(__riscv)
    return FIN_VOICE_ARCH_OTHER;
#else
    return FIN_VOICE_ARCH_UNKNOWN;
#endif
}

static FinVoiceEnvironment expected_compile_environment(void)
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

static uint64_t expected_default_budget(FinVoiceDeviceTier tier)
{
    switch (tier) {
    case FIN_VOICE_TIER_TINY:
        return UINT64_C(24) * TEST_KIB_BYTES;
    case FIN_VOICE_TIER_CONSTRAINED:
        return UINT64_C(64) * TEST_KIB_BYTES;
    case FIN_VOICE_TIER_LOW_END:
        return UINT64_C(128) * TEST_KIB_BYTES;
    case FIN_VOICE_TIER_BALANCED:
        return UINT64_C(256) * TEST_KIB_BYTES;
    case FIN_VOICE_TIER_PERFORMANCE:
        return FIN_VOICE_WORKING_SET_CAP_BYTES;
    case FIN_VOICE_TIER_UNKNOWN:
    default:
        return UINT64_C(128) * TEST_KIB_BYTES;
    }
}

static FinVoiceDeviceTier low_power_downgrade(FinVoiceDeviceTier tier)
{
    switch (tier) {
    case FIN_VOICE_TIER_PERFORMANCE:
        return FIN_VOICE_TIER_BALANCED;
    case FIN_VOICE_TIER_BALANCED:
        return FIN_VOICE_TIER_LOW_END;
    case FIN_VOICE_TIER_LOW_END:
        return FIN_VOICE_TIER_CONSTRAINED;
    case FIN_VOICE_TIER_CONSTRAINED:
        return FIN_VOICE_TIER_TINY;
    case FIN_VOICE_TIER_TINY:
    case FIN_VOICE_TIER_UNKNOWN:
    default:
        return tier;
    }
}

static int profile_detect_with_hints(
    const FinVoicePlatformHints *hints,
    FinVoicePlatformProfile *profile
)
{
    CHECK(fin_voice_platform_profile_detect(profile, hints));
    return profile->audio_sample_rate_hz == FIN_VOICE_DEFAULT_SAMPLE_RATE_HZ;
}

static void test_public_type_alignment(void)
{
    FinVoicePlatformProfile profile;
    FinVoicePlatformProfile profile_array[2];
    uintptr_t profile_address = (uintptr_t)&profile;
    uintptr_t architecture_address = (uintptr_t)&profile.architecture;
    uintptr_t simd_address = (uintptr_t)&profile.simd;

    CHECK(profile_address % FIN_VOICE_CACHE_LINE_BYTES == 0U);
    CHECK(architecture_address % FIN_VOICE_CACHE_LINE_BYTES == 0U);
    CHECK(simd_address % _Alignof(FinVoiceSimdContext) == 0U);
    CHECK(((uintptr_t)&profile_array[0] % FIN_VOICE_CACHE_LINE_BYTES) == 0U);
    CHECK(((uintptr_t)&profile_array[1] % FIN_VOICE_CACHE_LINE_BYTES) == 0U);
    CHECK((uintptr_t)&profile_array[1] - (uintptr_t)&profile_array[0] ==
          sizeof(FinVoicePlatformProfile));
}

static void test_default_hints(void)
{
    FinVoicePlatformHints hints;

    memset(&hints, 0xA5, sizeof(hints));
    fin_voice_platform_hints_default(&hints);

    CHECK(hints.logical_core_count == 0U);
    CHECK(hints.available_memory_bytes == UINT64_C(0));
    CHECK(hints.reported_l2_cache_bytes == UINT64_C(0));
    CHECK(hints.requested_working_set_bytes == UINT64_C(0));
    CHECK(hints.low_power_mode == false);

    /* Null is explicitly accepted as a no-op. */
    fin_voice_platform_hints_default(NULL);
}

static void test_working_set_clamp(void)
{
    const uint64_t cap = FIN_VOICE_WORKING_SET_CAP_BYTES;

    CHECK(cap == UINT64_C(512000));
    CHECK(fin_voice_platform_clamp_working_set(UINT64_C(0)) == UINT64_C(0));
    CHECK(fin_voice_platform_clamp_working_set(UINT64_C(1)) == UINT64_C(1));
    CHECK(fin_voice_platform_clamp_working_set(cap - UINT64_C(1)) == cap - UINT64_C(1));
    CHECK(fin_voice_platform_clamp_working_set(cap) == cap);
    CHECK(fin_voice_platform_clamp_working_set(cap + UINT64_C(1)) == cap);
    CHECK(fin_voice_platform_clamp_working_set(UINT64_MAX) == cap);
}

static void test_names(void)
{
    static const struct {
        FinVoiceArchitecture value;
        const char *name;
    } architecture_cases[] = {
        { FIN_VOICE_ARCH_UNKNOWN, "unknown" },
        { FIN_VOICE_ARCH_X86_32, "x86-32" },
        { FIN_VOICE_ARCH_X86_64, "x86-64" },
        { FIN_VOICE_ARCH_ARM_32, "ARM-32" },
        { FIN_VOICE_ARCH_ARM_64, "ARM-64" },
        { FIN_VOICE_ARCH_ESP32, "ESP32" },
        { FIN_VOICE_ARCH_RISCV_32, "RISC-V-32" },
        { FIN_VOICE_ARCH_RISCV_64, "RISC-V-64" },
        { FIN_VOICE_ARCH_WASM_32, "WASM-32" },
        { FIN_VOICE_ARCH_WASM_64, "WASM-64" },
        { FIN_VOICE_ARCH_OTHER, "other" }
    };
    static const struct {
        FinVoiceEnvironment value;
        const char *name;
    } environment_cases[] = {
        { FIN_VOICE_ENV_UNKNOWN, "unknown" },
        { FIN_VOICE_ENV_WINDOWS, "Windows" },
        { FIN_VOICE_ENV_LINUX, "Linux" },
        { FIN_VOICE_ENV_ANDROID, "Android" },
        { FIN_VOICE_ENV_APPLE, "Apple" },
        { FIN_VOICE_ENV_ESP_IDF, "ESP-IDF" },
        { FIN_VOICE_ENV_BARE_METAL, "bare-metal" },
        { FIN_VOICE_ENV_WASM, "WebAssembly" },
        { FIN_VOICE_ENV_OTHER, "other" }
    };
    static const struct {
        FinVoiceDeviceTier value;
        const char *name;
    } tier_cases[] = {
        { FIN_VOICE_TIER_UNKNOWN, "unknown" },
        { FIN_VOICE_TIER_TINY, "tiny" },
        { FIN_VOICE_TIER_CONSTRAINED, "constrained" },
        { FIN_VOICE_TIER_LOW_END, "low-end" },
        { FIN_VOICE_TIER_BALANCED, "balanced" },
        { FIN_VOICE_TIER_PERFORMANCE, "performance" }
    };
    size_t i;

    for (i = 0U; i < sizeof(architecture_cases) / sizeof(architecture_cases[0]); ++i) {
        const char *actual = fin_voice_architecture_name(architecture_cases[i].value);
        CHECK(actual != NULL);
        CHECK(strcmp(actual, architecture_cases[i].name) == 0);
    }
    for (i = 0U; i < sizeof(environment_cases) / sizeof(environment_cases[0]); ++i) {
        const char *actual = fin_voice_environment_name(environment_cases[i].value);
        CHECK(actual != NULL);
        CHECK(strcmp(actual, environment_cases[i].name) == 0);
    }
    for (i = 0U; i < sizeof(tier_cases) / sizeof(tier_cases[0]); ++i) {
        const char *actual = fin_voice_device_tier_name(tier_cases[i].value);
        CHECK(actual != NULL);
        CHECK(strcmp(actual, tier_cases[i].name) == 0);
    }

    CHECK(strcmp(fin_voice_architecture_name((FinVoiceArchitecture)-1), "unknown") == 0);
    CHECK(strcmp(fin_voice_environment_name((FinVoiceEnvironment)-1), "unknown") == 0);
    CHECK(strcmp(fin_voice_device_tier_name((FinVoiceDeviceTier)-1), "unknown") == 0);
}

static void test_detection_defaults_and_runtime_dispatch(void)
{
    FinVoicePlatformProfile profile;
    const FinVoiceDeviceTier detected_tier_expected =
        (expected_compile_architecture() == FIN_VOICE_ARCH_ESP32)
            ? FIN_VOICE_TIER_TINY
            : (is_fixed_constrained_architecture(expected_compile_architecture())
                ? FIN_VOICE_TIER_CONSTRAINED
                : ((is_known_64_bit_architecture(expected_compile_architecture()))
                    ? FIN_VOICE_TIER_BALANCED
                    : FIN_VOICE_TIER_UNKNOWN));

    memset(&profile, 0xA5, sizeof(profile));
    CHECK(profile_detect_with_hints(NULL, &profile));

    CHECK(profile.architecture == expected_compile_architecture());
    CHECK(profile.environment == expected_compile_environment());
    CHECK(profile.logical_core_count == 0U);
    CHECK(profile.available_memory_bytes == UINT64_C(0));
    CHECK(profile.reported_l2_cache_bytes == UINT64_C(0));
    CHECK(profile.low_power_mode == false);
    CHECK(profile.audio_sample_rate_hz == FIN_VOICE_DEFAULT_SAMPLE_RATE_HZ);
    CHECK(profile.audio_frame_samples == FIN_VOICE_DEFAULT_FRAME_SAMPLES);
    CHECK(profile.audio_hop_samples == FIN_VOICE_DEFAULT_HOP_SAMPLES);
    CHECK(profile.device_tier == detected_tier_expected);
    CHECK(profile.working_set_budget_bytes == expected_default_budget(profile.device_tier));

    CHECK(profile.selected_backend != FIN_VOICE_SIMD_AUTO);
    CHECK(fin_voice_simd_context_backend(&profile.simd) == profile.selected_backend);
    CHECK(fin_voice_simd_backend_available(profile.selected_backend));
    CHECK(profile.hardware_simd_selected ==
          (profile.selected_backend != FIN_VOICE_SIMD_SCALAR));
    CHECK(fin_voice_simd_backend_name(profile.selected_backend) != NULL);
    CHECK(strcmp(fin_voice_simd_backend_name(profile.selected_backend), "unknown") != 0);

    /* The public object and its aligned members remain aligned after detection. */
    CHECK((uintptr_t)&profile % FIN_VOICE_CACHE_LINE_BYTES == 0U);
    CHECK((uintptr_t)&profile.architecture % FIN_VOICE_CACHE_LINE_BYTES == 0U);
    CHECK((uintptr_t)&profile.simd % _Alignof(FinVoiceSimdContext) == 0U);

    CHECK(fin_voice_platform_profile_detect(NULL, NULL) == false);
}

static void test_hint_copy_and_budget_override(void)
{
    FinVoicePlatformHints hints;
    FinVoicePlatformProfile profile;

    fin_voice_platform_hints_default(&hints);
    hints.logical_core_count = 6U;
    hints.available_memory_bytes = UINT64_C(12) * TEST_GIB_BYTES;
    hints.reported_l2_cache_bytes = UINT64_C(2) * TEST_KIB_BYTES;
    hints.requested_working_set_bytes = UINT64_C(12345);
    hints.low_power_mode = true;

    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.logical_core_count == hints.logical_core_count);
    CHECK(profile.available_memory_bytes == hints.available_memory_bytes);
    CHECK(profile.reported_l2_cache_bytes == hints.reported_l2_cache_bytes);
    CHECK(profile.low_power_mode == hints.low_power_mode);
    CHECK(profile.working_set_budget_bytes == UINT64_C(12345));

    hints.requested_working_set_bytes = FIN_VOICE_WORKING_SET_CAP_BYTES;
    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.working_set_budget_bytes == FIN_VOICE_WORKING_SET_CAP_BYTES);

    hints.requested_working_set_bytes = FIN_VOICE_WORKING_SET_CAP_BYTES + UINT64_C(1);
    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.working_set_budget_bytes == FIN_VOICE_WORKING_SET_CAP_BYTES);

    /* Zero means use the tier's default, not a literal zero-byte budget. */
    hints.requested_working_set_bytes = UINT64_C(0);
    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.working_set_budget_bytes == expected_default_budget(profile.device_tier));
}

static void test_tier_inference_boundaries(void)
{
    FinVoicePlatformHints hints;
    FinVoicePlatformProfile profile;
    FinVoiceArchitecture arch;
    FinVoiceDeviceTier expected;

    fin_voice_platform_hints_default(&hints);
    CHECK(profile_detect_with_hints(&hints, &profile));
    arch = profile.architecture;

    /* ESP32 and 32-bit constrained targets intentionally dominate resource hints. */
    if (is_fixed_tiny_architecture(arch)) {
        expected = FIN_VOICE_TIER_TINY;
    } else if (is_fixed_constrained_architecture(arch)) {
        expected = FIN_VOICE_TIER_CONSTRAINED;
    } else {
        expected = FIN_VOICE_TIER_LOW_END;
    }
    hints.logical_core_count = 2U;
    hints.available_memory_bytes = UINT64_C(16) * TEST_GIB_BYTES;
    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.device_tier == expected);

    /* Exactly 1 GiB is still low-end when architecture is not hard-tiered. */
    fin_voice_platform_hints_default(&hints);
    hints.available_memory_bytes = TEST_GIB_BYTES;
    if (is_fixed_tiny_architecture(arch)) {
        expected = FIN_VOICE_TIER_TINY;
    } else if (is_fixed_constrained_architecture(arch)) {
        expected = FIN_VOICE_TIER_CONSTRAINED;
    } else {
        expected = FIN_VOICE_TIER_LOW_END;
    }
    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.device_tier == expected);

    /* Four cores takes the balanced branch when memory exceeds 4 GiB. */
    fin_voice_platform_hints_default(&hints);
    hints.logical_core_count = 4U;
    hints.available_memory_bytes = UINT64_C(8) * TEST_GIB_BYTES;
    if (is_fixed_tiny_architecture(arch)) {
        expected = FIN_VOICE_TIER_TINY;
    } else if (is_fixed_constrained_architecture(arch)) {
        expected = FIN_VOICE_TIER_CONSTRAINED;
    } else {
        expected = FIN_VOICE_TIER_BALANCED;
    }
    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.device_tier == expected);

    /* With core count unknown, exactly 4 GiB remains balanced. */
    fin_voice_platform_hints_default(&hints);
    hints.available_memory_bytes = UINT64_C(4) * TEST_GIB_BYTES;
    if (is_fixed_tiny_architecture(arch)) {
        expected = FIN_VOICE_TIER_TINY;
    } else if (is_fixed_constrained_architecture(arch)) {
        expected = FIN_VOICE_TIER_CONSTRAINED;
    } else {
        expected = FIN_VOICE_TIER_BALANCED;
    }
    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.device_tier == expected);

    /* One byte above 4 GiB crosses to performance if no fixed tier applies. */
    hints.available_memory_bytes = (UINT64_C(4) * TEST_GIB_BYTES) + UINT64_C(1);
    if (is_fixed_tiny_architecture(arch)) {
        expected = FIN_VOICE_TIER_TINY;
    } else if (is_fixed_constrained_architecture(arch)) {
        expected = FIN_VOICE_TIER_CONSTRAINED;
    } else {
        expected = FIN_VOICE_TIER_PERFORMANCE;
    }
    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.device_tier == expected);

    /* Five cores crosses the performance threshold unless the architecture is hard-tiered. */
    fin_voice_platform_hints_default(&hints);
    hints.logical_core_count = 5U;
    hints.available_memory_bytes = (UINT64_C(4) * TEST_GIB_BYTES) + UINT64_C(1);
    if (is_fixed_tiny_architecture(arch)) {
        expected = FIN_VOICE_TIER_TINY;
    } else if (is_fixed_constrained_architecture(arch)) {
        expected = FIN_VOICE_TIER_CONSTRAINED;
    } else {
        expected = FIN_VOICE_TIER_PERFORMANCE;
    }
    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.device_tier == expected);

    /* Low-power mode steps the inferred tier down by one level. */
    fin_voice_platform_hints_default(&hints);
    hints.logical_core_count = 8U;
    hints.available_memory_bytes = UINT64_C(8) * TEST_GIB_BYTES;
    CHECK(profile_detect_with_hints(&hints, &profile));
    expected = is_fixed_tiny_architecture(arch)
        ? FIN_VOICE_TIER_TINY
        : (is_fixed_constrained_architecture(arch)
            ? FIN_VOICE_TIER_CONSTRAINED
            : FIN_VOICE_TIER_PERFORMANCE);
    CHECK(profile.device_tier == expected);

    hints.low_power_mode = true;
    CHECK(profile_detect_with_hints(&hints, &profile));
    CHECK(profile.device_tier == low_power_downgrade(expected));
}

int main(void)
{
    test_public_type_alignment();
    test_default_hints();
    test_working_set_clamp();
    test_names();
    test_detection_defaults_and_runtime_dispatch();
    test_hint_copy_and_budget_override();
    test_tier_inference_boundaries();

    if (g_failures != 0U) {
        (void)fprintf(stderr, "Platform profile tests: %lu/%lu checks failed\n",
                      g_failures, g_checks);
        return EXIT_FAILURE;
    }

    (void)printf("Platform profile tests: %lu checks passed\n", g_checks);
    return EXIT_SUCCESS;
}
