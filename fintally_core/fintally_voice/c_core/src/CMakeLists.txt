# ===========================================================================
# Fintally Voice C Core — modular source targets
# Strict C23; ISA-specialized sources are isolated in a dedicated object target.
# ===========================================================================

# Text builder, SPSC, event queue and language FSM are independently complete
# modules. Each is built as its own object component; no runtime module owns or
# initializes a worker thread.
option(FIN_VOICE_BUILD_PLATFORM_COMPONENTS
    "Build platform profile and thermal workload policy"
    ON
)
option(FIN_VOICE_ENABLE_SIMD_DISPATCH
    "Build scalar and runtime-dispatched architecture SIMD backends"
    ON
)
option(FIN_VOICE_ENABLE_ESP_DSP
    "Enable the ESP-IDF ESP-DSP optimized backend"
    OFF
)

# ---------------------------------------------------------------------------
# Source groups
# ---------------------------------------------------------------------------

set(FIN_VOICE_QUANTIZATION_SOURCES
    vq_quantize.c
)

# The exporter owns these immutable tables. Compile the registry and its thin
# language accessors only when both generated artifacts exist. The accessor
# modules never embed or duplicate centroid arrays.
set(FIN_VOICE_GENERATED_CODEBOOK_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/generated_codebook_data.c")
set(FIN_VOICE_GENERATED_CODEBOOK_HEADER
    "${PROJECT_SOURCE_DIR}/include/codebooks/fin_voice_codebook_data.h")

if(EXISTS "${FIN_VOICE_GENERATED_CODEBOOK_SOURCE}" AND
   EXISTS "${FIN_VOICE_GENERATED_CODEBOOK_HEADER}")
    list(APPEND FIN_VOICE_QUANTIZATION_SOURCES
        generated_codebook_data.c
        codebook_en.c
        codebook_fr.c
        codebook_hi.c
        codebook_zh.c
        codebook_registry.c
    )
else()
    message(STATUS
        "FinTally Voice generated codebooks absent: language wrappers and registry are not built")
endif()

set(FIN_VOICE_DSP_SOURCES
    acoustic_dsp.c
    energy_zcr.c
    lpc_formants.c
    mfcc.c
    pitch_f0.c
)

# The SPSC ring is independently complete and is built into the core even
# while the other runtime primitives remain opt-in.
set(FIN_VOICE_SPSC_SOURCES
    spsc_ring.c
)

# The event queue is independently implemented and uses the SPSC ownership
# contract. Keep it enabled as a standalone component.
set(FIN_VOICE_EVENT_QUEUE_SOURCES
    event_queue.c
)

# Language state handling is deterministic, allocation-free, and independently
# complete. Keep it enabled even while text_builder.c remains unfinished.
set(FIN_VOICE_LANGUAGE_FSM_SOURCES
    language_fsm.c
)

# Bounded text construction accepts valid recognizer text only. It does not
# perform ASR or interpret centroid identifiers as words.
set(FIN_VOICE_TEXT_BUILDER_SOURCES
    text_builder.c
)

# Thin C ABI orchestration layer. This composes completed modules but keeps
# their implementations and ownership contracts separate from acoustic_dsp.c.
set(FIN_VOICE_PIPELINE_SOURCES)
if(EXISTS "${PROJECT_SOURCE_DIR}/include/codebooks/fin_voice_codebook_data.h"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated_codebook_data.c")
    list(APPEND FIN_VOICE_PIPELINE_SOURCES voice_pipeline.c)
else()
    message(STATUS
        "FinTally Voice pipeline façade disabled until generated codebook data exists")
endif()

# Stage 4 platform/thermal modules are now implemented and enabled by default.
set(FIN_VOICE_PLATFORM_SOURCES
    platform_profile.c
    thermal_policy.c
)

# Each backend source has a real operations-table definition on every target:
# supported intrinsics on the matching compiler/architecture, otherwise an
# unavailable stub. Therefore the dispatcher can select safely at runtime.
set(FIN_VOICE_SIMD_SOURCES
    kernel/simd_dispatch.c
    kernel/simd_scalar.c
    kernel/simd_x86.c
    kernel/simd_arm_neon.c
    kernel/simd_esp_dsp.c
)

# ---------------------------------------------------------------------------
# Validate only source groups selected for this build
# ---------------------------------------------------------------------------

set(FIN_VOICE_REQUIRED_SOURCES
    ${FIN_VOICE_DSP_SOURCES}
    ${FIN_VOICE_QUANTIZATION_SOURCES}
    ${FIN_VOICE_SPSC_SOURCES}
    ${FIN_VOICE_EVENT_QUEUE_SOURCES}
    ${FIN_VOICE_LANGUAGE_FSM_SOURCES}
    ${FIN_VOICE_TEXT_BUILDER_SOURCES}
    ${FIN_VOICE_PIPELINE_SOURCES}
)

if(FIN_VOICE_BUILD_PLATFORM_COMPONENTS)
    list(APPEND FIN_VOICE_REQUIRED_SOURCES ${FIN_VOICE_PLATFORM_SOURCES})
endif()

if(FIN_VOICE_ENABLE_SIMD_DISPATCH)
    list(APPEND FIN_VOICE_REQUIRED_SOURCES ${FIN_VOICE_SIMD_SOURCES})
endif()

foreach(source_file IN LISTS FIN_VOICE_REQUIRED_SOURCES)
    set(source_path "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")

    if(NOT EXISTS "${source_path}")
        message(FATAL_ERROR "Required C source file is missing: ${source_path}")
    endif()

    file(SIZE "${source_path}" source_size)
    if(source_size EQUAL 0)
        message(FATAL_ERROR
            "Selected source file is empty: ${source_path}. "
            "Complete its implementation or disable the corresponding optional component.")
    endif()
endforeach()

if(FIN_VOICE_BUILD_PLATFORM_COMPONENTS AND NOT FIN_VOICE_ENABLE_SIMD_DISPATCH)
    message(FATAL_ERROR
        "FIN_VOICE_BUILD_PLATFORM_COMPONENTS requires FIN_VOICE_ENABLE_SIMD_DISPATCH, "
        "because platform_profile.c embeds and initializes FinVoiceSimdContext.")
endif()

# ---------------------------------------------------------------------------
# Common object-component configuration
# ---------------------------------------------------------------------------

function(fin_voice_add_object_component target)
    add_library(${target} OBJECT ${ARGN})

    target_include_directories(${target}
        PRIVATE
            "${PROJECT_SOURCE_DIR}/include"
    )

    if(TARGET FintallyVoice::Dependencies)
        target_link_libraries(${target} PRIVATE FintallyVoice::Dependencies)
    else()
        message(FATAL_ERROR
            "Expected target FintallyVoice::Dependencies before add_subdirectory(src).")
    endif()

    fin_voice_configure_c_target(${target})
    fin_voice_apply_sanitizers(${target})
endfunction()

# ---------------------------------------------------------------------------
# Acoustic DSP component
# ---------------------------------------------------------------------------

fin_voice_add_object_component(
    fin_voice_dsp_objects
    ${FIN_VOICE_DSP_SOURCES}
)

# ---------------------------------------------------------------------------
# Vector quantization component
# ---------------------------------------------------------------------------

fin_voice_add_object_component(
    fin_voice_quantization_objects
    ${FIN_VOICE_QUANTIZATION_SOURCES}
)

fin_voice_add_object_component(
    fin_voice_spsc_objects
    ${FIN_VOICE_SPSC_SOURCES}
)

fin_voice_add_object_component(
    fin_voice_event_queue_objects
    ${FIN_VOICE_EVENT_QUEUE_SOURCES}
)

fin_voice_add_object_component(
    fin_voice_language_fsm_objects
    ${FIN_VOICE_LANGUAGE_FSM_SOURCES}
)

set(FIN_VOICE_CORE_OBJECT_TARGETS
    fin_voice_dsp_objects
    fin_voice_quantization_objects
    fin_voice_spsc_objects
    fin_voice_event_queue_objects
    fin_voice_language_fsm_objects
)

# ---------------------------------------------------------------------------
# Bounded text builder — standalone allocation-free component
# ---------------------------------------------------------------------------

fin_voice_add_object_component(
    fin_voice_text_builder_objects
    ${FIN_VOICE_TEXT_BUILDER_SOURCES}
)

list(APPEND FIN_VOICE_CORE_OBJECT_TARGETS fin_voice_text_builder_objects)

# ---------------------------------------------------------------------------
# Unified C ABI orchestration façade
# ---------------------------------------------------------------------------

if(FIN_VOICE_PIPELINE_SOURCES)
    fin_voice_add_object_component(
        fin_voice_pipeline_objects
        ${FIN_VOICE_PIPELINE_SOURCES}
    )
    list(APPEND FIN_VOICE_CORE_OBJECT_TARGETS fin_voice_pipeline_objects)
endif()

# ---------------------------------------------------------------------------
# Platform profile and thermal policy
# ---------------------------------------------------------------------------

if(FIN_VOICE_BUILD_PLATFORM_COMPONENTS)
    fin_voice_add_object_component(
        fin_voice_platform_objects
        ${FIN_VOICE_PLATFORM_SOURCES}
    )

    list(APPEND FIN_VOICE_CORE_OBJECT_TARGETS fin_voice_platform_objects)
endif()

# ---------------------------------------------------------------------------
# Runtime SIMD dispatch and ISA-specific backends
# ---------------------------------------------------------------------------

if(FIN_VOICE_ENABLE_SIMD_DISPATCH)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/include/kernel/simd_dispatch.h")
        message(FATAL_ERROR
            "SIMD dispatch enabled, but include/kernel/simd_dispatch.h is missing.")
    endif()

    fin_voice_add_object_component(
        fin_voice_simd_objects
        ${FIN_VOICE_SIMD_SOURCES}
    )

    # These indicate that the backend operations tables are linked into this
    # binary. They do NOT mean the current processor supports each ISA.
    # Each table's is_supported() function must check hardware and OS state.
    target_compile_definitions(fin_voice_simd_objects PRIVATE
        FIN_VOICE_HAVE_SIMD_SSE42=1
        FIN_VOICE_HAVE_SIMD_NEON=1
        FIN_VOICE_HAVE_SIMD_AVX2=1
        FIN_VOICE_HAVE_SIMD_AVX2_FMA=1
        FIN_VOICE_HAVE_SIMD_AVX_VNNI=1
        FIN_VOICE_HAVE_SIMD_AVX512=1
        FIN_VOICE_HAVE_SIMD_ESP_DSP=1
    )

    if(FIN_VOICE_ENABLE_ESP_DSP)
        # This optional function provides the ESP-DSP include directory and
        # compile definition to the backend object target. The final static
        # library also needs the external link dependency below.
        if(NOT COMMAND fin_voice_configure_dependencies)
            message(FATAL_ERROR
                "ESP-DSP was enabled, but cmake/Dependencies.cmake does not "
                "provide fin_voice_configure_dependencies(target).")
        endif()
        fin_voice_configure_dependencies(fin_voice_simd_objects)
    endif()

    list(APPEND FIN_VOICE_CORE_OBJECT_TARGETS fin_voice_simd_objects)
endif()

# ---------------------------------------------------------------------------
# Final static library used by Rust FFI
# ---------------------------------------------------------------------------

set(FIN_VOICE_CORE_OBJECTS)
foreach(component_target IN LISTS FIN_VOICE_CORE_OBJECT_TARGETS)
    list(APPEND FIN_VOICE_CORE_OBJECTS "$<TARGET_OBJECTS:${component_target}>")
endforeach()

add_library(fintally_voice_core STATIC ${FIN_VOICE_CORE_OBJECTS})
add_library(FintallyVoice::core ALIAS fintally_voice_core)

set_target_properties(fintally_voice_core PROPERTIES
    OUTPUT_NAME "fintally_voice_core"
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(fintally_voice_core
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(fintally_voice_core PUBLIC FintallyVoice::Dependencies)
fin_voice_configure_c_target(fintally_voice_core)
fin_voice_apply_sanitizers(fintally_voice_core)

if(UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL "Generic")
    target_link_libraries(fintally_voice_core PUBLIC m)
endif()

if(FIN_VOICE_ENABLE_ESP_DSP)
    # Object-library dependencies do not reliably propagate when its objects
    # are injected through $<TARGET_OBJECTS:...>; attach the ESP-DSP link
    # dependency to the final archive target as well.
    fin_voice_configure_dependencies(fintally_voice_core)
endif()

message(STATUS "FinTally Voice: acoustic DSP and quantization enabled")
message(STATUS "FinTally Voice: SPSC ring = ON (standalone component)")
message(STATUS "FinTally Voice: bounded event queue = ON (SPSC contract)")
message(STATUS "FinTally Voice: language FSM = ON (standalone component)")
message(STATUS "FinTally Voice: bounded text builder = ON (standalone component)")
message(STATUS "FinTally Voice: unified pipeline façade = ${FIN_VOICE_PIPELINE_SOURCES}")
message(STATUS "FinTally Voice: platform/thermal components = ${FIN_VOICE_BUILD_PLATFORM_COMPONENTS}")
message(STATUS "FinTally Voice: SIMD dispatch = ${FIN_VOICE_ENABLE_SIMD_DISPATCH}")
message(STATUS "FinTally Voice: ESP-DSP = ${FIN_VOICE_ENABLE_ESP_DSP}")
