cmake_minimum_required(VERSION 3.25)

project(
    FintallyVoiceCore
    VERSION 0.1.0
    DESCRIPTION "Portable C23 acoustic DSP and runtime-dispatched integer SIMD core"
    LANGUAGES C
)

# ---------------------------------------------------------------------------
# Module discovery
# ---------------------------------------------------------------------------

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Canonical language/build settings
# ---------------------------------------------------------------------------

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
    "Generate compile_commands.json for tooling" FORCE
)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Build options — keep these names aligned with src/CMakeLists.txt
# ---------------------------------------------------------------------------

option(FIN_VOICE_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors" OFF
)

option(FIN_VOICE_ENABLE_THREADS
    "Link the platform threading dependency when required" OFF
)

option(FIN_VOICE_ENABLE_SIMD_DISPATCH
    "Build scalar and runtime-dispatched SIMD backends" ON
)

option(FIN_VOICE_BUILD_PLATFORM_COMPONENTS
    "Build platform profile and thermal workload policy" ON
)

option(FIN_VOICE_ENABLE_ESP_DSP
    "Enable ESP-IDF ESP-DSP integration" OFF
)

option(FIN_VOICE_BUILD_TESTS
    "Build the C-core test suite" OFF
)

option(FIN_VOICE_BUILD_BENCHMARKS
    "Build standalone DSP benchmarks" OFF
)

option(FIN_VOICE_ENABLE_ASAN_UBSAN
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF
)

option(FIN_VOICE_ENABLE_TSAN
    "Enable ThreadSanitizer (mutually exclusive with ASan/UBSan)" OFF
)

set(FIN_VOICE_ESP_DSP_ROOT "" CACHE PATH
    "Optional esp-dsp source/component root"
)
set(FIN_VOICE_ESP_DSP_LIBRARY "" CACHE FILEPATH
    "Optional prebuilt ESP-DSP library for standalone toolchains"
)
set(FIN_VOICE_ESP_DSP_INCLUDE_DIR "" CACHE PATH
    "Optional directory containing dsps_dotprod.h"
)

# Backwards compatibility for cache variables used by the older root file.
# New builds should use FIN_VOICE_ENABLE_ASAN_UBSAN / FIN_VOICE_ENABLE_TSAN.
option(FIN_VOICE_SANITIZE_ADDRESS
    "Legacy option: enable AddressSanitizer" OFF
)
option(FIN_VOICE_SANITIZE_UNDEFINED
    "Legacy option: enable UndefinedBehaviorSanitizer" OFF
)
option(FIN_VOICE_SANITIZE_THREAD
    "Legacy option: enable ThreadSanitizer" OFF
)

if(FIN_VOICE_SANITIZE_ADDRESS OR FIN_VOICE_SANITIZE_UNDEFINED)
    set(FIN_VOICE_ENABLE_ASAN_UBSAN ON)
endif()

if(FIN_VOICE_SANITIZE_THREAD)
    set(FIN_VOICE_ENABLE_TSAN ON)
endif()

if(FIN_VOICE_ENABLE_ASAN_UBSAN AND FIN_VOICE_ENABLE_TSAN)
    message(FATAL_ERROR
        "ASan/UBSan and TSan cannot be enabled together in the same build."
    )
endif()

# ---------------------------------------------------------------------------
# Common build infrastructure
# ---------------------------------------------------------------------------

include(CompilerOptions)
include(Dependencies)
include(Sanitizers)

message(STATUS "FinTally Voice target system : ${CMAKE_SYSTEM_NAME}")
message(STATUS "FinTally Voice target CPU    : ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "FinTally Voice C compiler    : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "FinTally Voice C standard   : strict C23; extensions disabled")
message(STATUS "FinTally Voice SIMD dispatch: ${FIN_VOICE_ENABLE_SIMD_DISPATCH}")
message(STATUS "FinTally Voice platform/thermal: ${FIN_VOICE_BUILD_PLATFORM_COMPONENTS}")
message(STATUS "FinTally Voice text builder: ON (standalone bounded component)")
message(STATUS "FinTally Voice ESP-DSP      : ${FIN_VOICE_ENABLE_ESP_DSP}")

# IMPORTANT:
# src/CMakeLists.txt creates the object component targets and then creates
# fintally_voice_core from their object files. Do NOT create fintally_voice_core
# here as that would conflict with that modular source-owned target model.
add_subdirectory(src)

if(NOT TARGET fintally_voice_core)
    message(FATAL_ERROR
        "src/CMakeLists.txt did not create the expected fintally_voice_core target."
    )
endif()

# Build-tree alias for consumers/tests. The source subdirectory also retains
# the existing FintallyVoice::core alias for compatibility.
if(NOT TARGET FintallyVoice::Core)
    add_library(FintallyVoice::Core ALIAS fintally_voice_core)
endif()

set_target_properties(fintally_voice_core PROPERTIES
    EXPORT_NAME Core
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    POSITION_INDEPENDENT_CODE ON
)

if(FIN_VOICE_ENABLE_THREADS)
    find_package(Threads REQUIRED)
    target_link_libraries(fintally_voice_core PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Tests and benchmarks — optional, based on the existing subdirectories
# ---------------------------------------------------------------------------

if(FIN_VOICE_BUILD_TESTS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        message(FATAL_ERROR
            "FIN_VOICE_BUILD_TESTS is ON, but tests/CMakeLists.txt is missing."
        )
    endif()

    enable_testing()
    add_subdirectory(tests)
endif()

if(FIN_VOICE_BUILD_BENCHMARKS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
        message(FATAL_ERROR
            "FIN_VOICE_BUILD_BENCHMARKS is ON, but benchmarks/CMakeLists.txt is missing."
        )
    endif()

    add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------

install(TARGETS fintally_voice_core fintally_voice_dependencies
    EXPORT FintallyVoiceTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT FintallyVoiceTargets
    FILE FintallyVoiceTargets.cmake
    NAMESPACE FintallyVoice::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FintallyVoice"
)
