cmake_minimum_required(VERSION 3.16)

# ==============================================================================
# FinOCR Native OCR Engine
# ==============================================================================

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(
    fin_ocr_native
    VERSION 1.0.0
    LANGUAGES C CXX
)

# ==============================================================================
# GLOBAL C++ CONFIGURATION
# ==============================================================================

set(
    CMAKE_CXX_STANDARD
    17
)

set(
    CMAKE_CXX_STANDARD_REQUIRED
    ON
)

set(
    CMAKE_CXX_EXTENSIONS
    OFF
)

# clangd / clang-tidy / static-analysis support.
set(
    CMAKE_EXPORT_COMPILE_COMMANDS
    ON
)

# ==============================================================================
# BUILD OPTIONS
# ==============================================================================

option(
    FIN_BUILD_SHARED
    "Build FinOCR as a shared library"
    OFF
)

option(
    FIN_ENABLE_NATIVE
    "Enable compiler -march=native optimizations"
    ON
)

option(
    FIN_ENABLE_AVX2
    "Enable AVX2 instructions"
    ON
)

option(
    FIN_ENABLE_AVX512
    "Enable AVX512 instructions"
    ON
)

option(
    FIN_ENABLE_LTO
    "Enable Link Time Optimization / IPO"
    OFF
)

option(
    FIN_ENABLE_TESSERACT
    "Enable Tesseract LSTM OCR backend"
    ON
)

option(
    FIN_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors"
    OFF
)

# ==============================================================================
# SANITIZER OPTIONS
# ==============================================================================

option(
    FIN_SANITIZER_ADDRESS
    "Enable AddressSanitizer"
    OFF
)

option(
    FIN_SANITIZER_UNDEFINED
    "Enable UndefinedBehaviorSanitizer"
    OFF
)

option(
    FIN_SANITIZER_LEAK
    "Enable LeakSanitizer"
    OFF
)

# ==============================================================================
# PROJECT CMAKE MODULE PATH
# ==============================================================================

list(
    APPEND
    CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

# ==============================================================================
# INTERNAL CMAKE MODULES
#
# CompilerOptions.cmake
#     compiler flags / CPU configuration
#
# Dependencies.cmake
#     Tesseract / Leptonica / PDFium / stb
#
# Sanitizers.cmake
#     ASan / UBSan / LSan
# ==============================================================================

include(
    CompilerOptions
)

include(
    Dependencies
)

include(
    Sanitizers
)

# ==============================================================================
# DEPENDENCY SUMMARY
# ==============================================================================

message(STATUS "")
message(STATUS "============================================================")
message(STATUS " FinOCR Native Configuration")
message(STATUS "============================================================")
message(STATUS "[FinOCR] Version            : ${PROJECT_VERSION}")
message(STATUS "[FinOCR] C++ Standard       : C++${CMAKE_CXX_STANDARD}")
message(STATUS "[FinOCR] Generator          : ${CMAKE_GENERATOR}")
message(STATUS "[FinOCR] Build Type         : ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "[FinOCR] Shared Library     : ${FIN_BUILD_SHARED}")
message(STATUS "[FinOCR] Native CPU         : ${FIN_ENABLE_NATIVE}")
message(STATUS "[FinOCR] AVX2               : ${FIN_ENABLE_AVX2}")
message(STATUS "[FinOCR] AVX512             : ${FIN_ENABLE_AVX512}")
message(STATUS "[FinOCR] LTO                : ${FIN_ENABLE_LTO}")
message(STATUS "[FinOCR] Tesseract          : ${FIN_ENABLE_TESSERACT}")
message(STATUS "[FinOCR] Warnings as Errors : ${FIN_WARNINGS_AS_ERRORS}")
message(STATUS "")
message(STATUS "[FinOCR] ASan               : ${FIN_SANITIZER_ADDRESS}")
message(STATUS "[FinOCR] UBSan              : ${FIN_SANITIZER_UNDEFINED}")
message(STATUS "[FinOCR] LSan               : ${FIN_SANITIZER_LEAK}")
message(STATUS "============================================================")
message(STATUS "")

# ==============================================================================
# CORE NATIVE LIBRARY
# ==============================================================================
#
# src/CMakeLists.txt is now responsible for:
#
#     - creating fin_ocr_native
#     - adding src/ffi_bridge.cpp
#     - adding src/thermal_sensor.cpp
#     - adding src/matrix_matcher.cpp facade
#     - adding all module subdirectories
#     - linking module/dependency targets
#
# The root deliberately does not enumerate implementation files.
# ==============================================================================

add_subdirectory(
    src
)

# ==============================================================================
# TESTING
# ==============================================================================
#
# Tests remain an independent build layer.
#
# tests/CMakeLists.txt owns:
#
#     unit_tests
#     dataset_tests
#     tensor_ops_tests
# ==============================================================================

enable_testing()

add_subdirectory(
    tests
)

# ==============================================================================
# BENCHMARKS
# ==============================================================================
#
# benchmarks/CMakeLists.txt owns:
#
#     bench_runner
#     bench_dataset
#     bench_tensor_ops
#     bench_matrix_matcher
#     bench_dataset_diagnostic
# ==============================================================================

add_subdirectory(
    benchmarks
)

# ==============================================================================
# LTO / IPO
# ==============================================================================
#
# Applied to the production native library after its target exists.
# ==============================================================================

if(FIN_ENABLE_LTO)

    include(
        CheckIPOSupported
    )

    check_ipo_supported(
        RESULT
            FIN_IPO_SUPPORTED
        OUTPUT
            FIN_IPO_ERROR
    )

    if(FIN_IPO_SUPPORTED)

        set_target_properties(
            fin_ocr_native
            PROPERTIES
                INTERPROCEDURAL_OPTIMIZATION TRUE
        )

        message(
            STATUS
            "[FinOCR] IPO/LTO enabled"
        )

    else()

        message(
            WARNING
            "[FinOCR] IPO/LTO requested but compiler/toolchain "
            "does not support it:\n${FIN_IPO_ERROR}"
        )

    endif()

endif()

# ==============================================================================
# FINAL TARGET CONFIGURATION
# ==============================================================================
#
# These calls happen after src/CMakeLists.txt creates the target.
# ==============================================================================

if(TARGET fin_ocr_native)

    fin_configure_native_target(
        fin_ocr_native
    )

    fin_apply_sanitizers(
        fin_ocr_native
    )

endif()

# ==============================================================================
# FINAL BUILD SUMMARY
# ==============================================================================

message(STATUS "")
message(STATUS "============================================================")
message(STATUS " FinOCR Target Graph")
message(STATUS "============================================================")

if(TARGET fin_ocr_native)

    get_target_property(
        FIN_OCR_LIBRARY_TYPE
        fin_ocr_native
        TYPE
    )

    message(
        STATUS
        "[FinOCR] Native target : fin_ocr_native"
    )

    message(
        STATUS
        "[FinOCR] Library type  : ${FIN_OCR_LIBRARY_TYPE}"
    )

else()

    message(
        FATAL_ERROR
        "[FinOCR] fin_ocr_native target was not created by src/CMakeLists.txt"
    )

endif()

if(TARGET unit_tests)

    message(
        STATUS
        "[FinOCR] Unit tests    : ENABLED"
    )

endif()

if(TARGET dataset_tests)

    message(
        STATUS
        "[FinOCR] Dataset tests : ENABLED"
    )

endif()

if(TARGET tensor_ops_tests)

    message(
        STATUS
        "[FinOCR] Tensor tests  : ENABLED"
    )

endif()

if(TARGET bench_runner)

    message(
        STATUS
        "[FinOCR] Benchmarks    : ENABLED"
    )

endif()

message(STATUS "============================================================")
message(STATUS "")
