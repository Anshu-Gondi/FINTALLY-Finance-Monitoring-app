if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_acoustic_dsp.c")
    message(FATAL_ERROR
        "Tests were enabled, but tests/test_acoustic_dsp.c does not exist. "
        "Add the test source before enabling FIN_VOICE_BUILD_TESTS."
    )
endif()

add_executable(
    test_acoustic_dsp
    test_acoustic_dsp.c
)

target_link_libraries(
    test_acoustic_dsp
    PRIVATE
        FintallyVoice::core
)

fin_voice_configure_c_target(test_acoustic_dsp)
fin_voice_apply_sanitizers(test_acoustic_dsp)

add_test(
    NAME acoustic_dsp
    COMMAND test_acoustic_dsp
)

# SIMD tests require the dispatch API and implementation to be built.
if(FIN_VOICE_ENABLE_SIMD_DISPATCH)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_simd_dispatch.c")
        message(FATAL_ERROR
            "FIN_VOICE_ENABLE_SIMD_DISPATCH and FIN_VOICE_BUILD_TESTS are ON, "
            "but tests/test_simd_dispatch.c is missing."
        )
    endif()

    add_executable(
        test_simd_dispatch
        test_simd_dispatch.c
    )

    target_link_libraries(
        test_simd_dispatch
        PRIVATE
            FintallyVoice::core
    )

    fin_voice_configure_c_target(test_simd_dispatch)
    fin_voice_apply_sanitizers(test_simd_dispatch)

    add_test(
        NAME simd_dispatch
        COMMAND test_simd_dispatch
    )
endif()

# Pitch estimator tests are kept separate so failures identify F0-specific
# behavior without conflating it with the full acoustic feature pipeline.
add_executable(
    test_pitch_f0
    test_pitch_f0.c
)

target_link_libraries(
    test_pitch_f0
    PRIVATE
        FintallyVoice::core
)

fin_voice_configure_c_target(test_pitch_f0)
fin_voice_apply_sanitizers(test_pitch_f0)

add_test(
    NAME pitch_f0
    COMMAND test_pitch_f0
)

# Focused LPC/formant tests isolate envelope selection and boundary behavior.
add_executable(
    test_lpc_formants
    test_lpc_formants.c
)

target_link_libraries(
    test_lpc_formants
    PRIVATE
        FintallyVoice::core
)

fin_voice_configure_c_target(test_lpc_formants)
fin_voice_apply_sanitizers(test_lpc_formants)

add_test(
    NAME lpc_formants
    COMMAND test_lpc_formants
)

# MFCC regression tests verify the filterbank/DCT contract and the explicitly
# aligned public plan/workspace layouts. Keep this test separate from the full
# acoustic pipeline so errors identify the MFCC module directly.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_mfcc.c")
    message(FATAL_ERROR
        "MFCC tests are enabled, but tests/test_mfcc.c is missing.")
endif()

add_executable(
    test_mfcc
    test_mfcc.c
)

target_link_libraries(
    test_mfcc
    PRIVATE
        FintallyVoice::core
)

fin_voice_configure_c_target(test_mfcc)
fin_voice_apply_sanitizers(test_mfcc)

add_test(
    NAME mfcc
    COMMAND test_mfcc
)

# Platform profile tests run only when the platform/thermal component is part
# of the core build. This avoids creating a test target with unresolved symbols
# in scalar/acoustic-only configurations that intentionally omit this module.
if(FIN_VOICE_BUILD_PLATFORM_COMPONENTS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_platform_profile.c")
        message(FATAL_ERROR
            "FIN_VOICE_BUILD_PLATFORM_COMPONENTS and FIN_VOICE_BUILD_TESTS are ON, "
            "but tests/test_platform_profile.c is missing.")
    endif()

    add_executable(
        test_platform_profile
        test_platform_profile.c
    )

    target_link_libraries(
        test_platform_profile
        PRIVATE
            FintallyVoice::core
    )

    fin_voice_configure_c_target(test_platform_profile)
    fin_voice_apply_sanitizers(test_platform_profile)

    add_test(
        NAME platform_profile
        COMMAND test_platform_profile
    )
endif()

# Thermal policy tests run only when the platform/thermal component is part
# of the core build. They operate on caller-supplied telemetry and need no
# hardware sensor or platform SDK.
if(FIN_VOICE_BUILD_PLATFORM_COMPONENTS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_thermal_policy.c")
        message(FATAL_ERROR
            "FIN_VOICE_BUILD_PLATFORM_COMPONENTS and FIN_VOICE_BUILD_TESTS are ON, "
            "but tests/test_thermal_policy.c is missing.")
    endif()

    add_executable(
        test_thermal_policy
        test_thermal_policy.c
    )

    target_link_libraries(
        test_thermal_policy
        PRIVATE
            FintallyVoice::core
    )

    fin_voice_configure_c_target(test_thermal_policy)
    fin_voice_apply_sanitizers(test_thermal_policy)

    add_test(
        NAME thermal_policy
        COMMAND test_thermal_policy
    )
endif()


# Generated-codebook VQ regression tests. This test deliberately consumes the
# exporter-generated C table through the public generated header. The canonical
# generated header/source are read-only build inputs and are never edited here.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../src/generated_codebook_data.c"
   AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../include/codebooks/fin_voice_codebook_data.h")
    add_executable(
        test_vq_quantize
        test_vq_quantize.c
    )

    target_link_libraries(
        test_vq_quantize
        PRIVATE
            FintallyVoice::core
    )

    fin_voice_configure_c_target(test_vq_quantize)
    fin_voice_apply_sanitizers(test_vq_quantize)

    add_test(
        NAME vq_quantize
        COMMAND test_vq_quantize
    )
else()
    message(STATUS
        "Skipping generated-codebook VQ regression test: "
        "offline_builder must generate include/codebooks/fin_voice_codebook_data.h "
        "and src/generated_codebook_data.c first.")
endif()


# Registry contract tests. These run only when the exporter-generated bundle
# and the registry implementation are present in the same build.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../src/generated_codebook_data.c"
   AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../include/codebooks/fin_voice_codebook_data.h"
   AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../src/codebook_registry.c"
   AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/test_codebook_registry.c")
    add_executable(
        test_codebook_registry
        test_codebook_registry.c
    )

    target_link_libraries(
        test_codebook_registry
        PRIVATE
            FintallyVoice::core
    )

    fin_voice_configure_c_target(test_codebook_registry)
    fin_voice_apply_sanitizers(test_codebook_registry)

    add_test(
        NAME codebook_registry
        COMMAND test_codebook_registry
    )
endif()


# SPSC ring is a standalone core component. The concurrency stress test uses
# C11 threads only in the host-side test executable; production code has no
# threading-library dependency and creates no threads of its own.
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/test_spsc_ring.c")
    message(FATAL_ERROR
        "SPSC tests are enabled, but tests/test_spsc_ring.c is missing.")
endif()

find_package(Threads REQUIRED)

add_executable(
    test_spsc_ring
    test_spsc_ring.c
)

target_link_libraries(
    test_spsc_ring
    PRIVATE
        FintallyVoice::core
        Threads::Threads
)

fin_voice_configure_c_target(test_spsc_ring)
fin_voice_apply_sanitizers(test_spsc_ring)

add_test(
    NAME spsc_ring
    COMMAND test_spsc_ring
)


# Bounded event queue. Its producer/consumer stress test is host-only; the
# production library depends solely on C atomics and caller-owned storage.
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/test_event_queue.c")
    message(FATAL_ERROR
        "Event queue tests are enabled, but tests/test_event_queue.c is missing.")
endif()

add_executable(
    test_event_queue
    test_event_queue.c
)

target_link_libraries(
    test_event_queue
    PRIVATE
        FintallyVoice::core
        Threads::Threads
)

fin_voice_configure_c_target(test_event_queue)
fin_voice_apply_sanitizers(test_event_queue)

add_test(
    NAME event_queue
    COMMAND test_event_queue
)


# Deterministic language state machine. The test covers state mechanics and
# rejection behavior only; model accuracy requires separate labelled-corpus
# evaluation and is intentionally not claimed by this unit test.
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/test_language_fsm.c")
    message(FATAL_ERROR
        "Language FSM tests are enabled, but tests/test_language_fsm.c is missing.")
endif()

add_executable(
    test_language_fsm
    test_language_fsm.c
)

target_link_libraries(
    test_language_fsm
    PRIVATE
        FintallyVoice::core
)

fin_voice_configure_c_target(test_language_fsm)
fin_voice_apply_sanitizers(test_language_fsm)

add_test(
    NAME language_fsm
    COMMAND test_language_fsm
)

# Bounded text assembly accepts only real UTF-8 transcript chunks. This test
# covers capacity, Unicode boundaries, flush/reset, and explicit overflow;
# it intentionally does not infer text from VQ centroid identifiers.
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/test_text_builder.c")
    message(FATAL_ERROR
        "Text-builder tests are enabled, but tests/test_text_builder.c is missing.")
endif()

add_executable(
    test_text_builder
    test_text_builder.c
)

target_link_libraries(
    test_text_builder
    PRIVATE
        FintallyVoice::core
)

fin_voice_configure_c_target(test_text_builder)
fin_voice_apply_sanitizers(test_text_builder)

add_test(
    NAME text_builder
    COMMAND test_text_builder
)



# Unified façade contract test. It links the real DSP, generated codebooks, VQ,
# language FSM, event queue, and text builder without merging module sources.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../src/voice_pipeline.c"
   AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../src/generated_codebook_data.c"
   AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../include/codebooks/fin_voice_codebook_data.h")
    add_executable(
        test_voice_pipeline
        test_voice_pipeline.c
    )

    target_link_libraries(
        test_voice_pipeline
        PRIVATE
            FintallyVoice::core
    )

    fin_voice_configure_c_target(test_voice_pipeline)
    fin_voice_apply_sanitizers(test_voice_pipeline)

    add_test(
        NAME voice_pipeline
        COMMAND test_voice_pipeline
    )
endif()
