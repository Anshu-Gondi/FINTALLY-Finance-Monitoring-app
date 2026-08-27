cmake_minimum_required(VERSION 3.16)

# Prevent FetchContent timestamp policy warnings (CMP0135)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(fin_ocr_native VERSION 1.0.0 LANGUAGES C CXX)

# ----------------------------------------------------------------------
# C++ Configuration
# ----------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd, LSP, and static analyzers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------------------
# Build Configuration Options
# ----------------------------------------------------------------------

option(
    FIN_BUILD_SHARED
    "Build shared library instead of static"
    OFF
)

option(
    FIN_ENABLE_NATIVE
    "Enable -march=native CPU optimizations"
    ON
)

option(
    FIN_ENABLE_AVX2
    "Enable AVX2 vector instructions"
    ON
)

option(
    FIN_ENABLE_AVX512
    "Enable AVX512 vector instructions"
    ON
)

# CRITICAL FIX: Default LTO to OFF for static FFI/Rust integration
option(
    FIN_ENABLE_LTO
    "Enable Link Time Optimization (IPO/LTO)"
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

include(FetchContent)

# ----------------------------------------------------------------------
# External Dependencies
# ----------------------------------------------------------------------

# ----------------------------------------------------------------------
# 1. pkg-config
#
# Used for:
#   - PDFium discovery
#   - Tesseract discovery
#   - Leptonica discovery
# ----------------------------------------------------------------------

find_package(PkgConfig REQUIRED)

# ----------------------------------------------------------------------
# 2. Tesseract OCR + Leptonica
# ----------------------------------------------------------------------

if(FIN_ENABLE_TESSERACT)

    message(
        STATUS
        "[FinOCR] Tesseract OCR backend ENABLED"
    )

    pkg_check_modules(
        TESSERACT
        REQUIRED
        IMPORTED_TARGET
        tesseract
    )

    pkg_check_modules(
        LEPTONICA
        REQUIRED
        IMPORTED_TARGET
        lept
    )

else()

    message(
        STATUS
        "[FinOCR] Tesseract OCR backend DISABLED"
    )

endif()

# ----------------------------------------------------------------------
# 3. stb_image
# ----------------------------------------------------------------------

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)

FetchContent_MakeAvailable(stb)

# ----------------------------------------------------------------------
# 4. PDFium
# ----------------------------------------------------------------------

pkg_check_modules(
    PDFIUM
    QUIET
    pdfium
)

if(NOT PDFIUM_FOUND)

    set(
        PDFIUM_ROOT_DIR
        ""
        CACHE PATH
        "Path to PDFium installation directory"
    )

    if(PDFIUM_ROOT_DIR)

        set(
            PDFIUM_INCLUDE_DIRS
            "${PDFIUM_ROOT_DIR}/include"
        )

        if(EXISTS "${PDFIUM_ROOT_DIR}/lib/libpdfium.so")

            set(
                PDFIUM_LIBRARIES
                "${PDFIUM_ROOT_DIR}/lib/libpdfium.so"
            )

        else()

            set(
                PDFIUM_LIBRARIES
                "${PDFIUM_ROOT_DIR}/lib/pdfium.lib"
            )

        endif()

    else()

        message(
            STATUS
            "[FinOCR] PDFium not found locally. "
            "Fetching prebuilt PDFium binaries..."
        )

        FetchContent_Declare(
            pdfium_binaries
            URL https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F6531/pdfium-linux-x64.tgz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )

        FetchContent_MakeAvailable(pdfium_binaries)

        set(
            PDFIUM_INCLUDE_DIRS
            "${pdfium_binaries_SOURCE_DIR}/include"
        )

        set(
            PDFIUM_LIBRARIES
            "${pdfium_binaries_SOURCE_DIR}/lib/libpdfium.so"
        )

    endif()

endif()

# ----------------------------------------------------------------------
# CPU / Compiler Optimization Helper
# ----------------------------------------------------------------------

function(fin_apply_cpu_optimizations TARGET)

    if(MSVC)

        target_compile_options(
            ${TARGET}
            PRIVATE

                /O2
                /fp:fast
                /W4
                /permissive-
        )

        if(FIN_WARNINGS_AS_ERRORS)

            target_compile_options(
                ${TARGET}
                PRIVATE
                    /WX
            )

        endif()

        if(FIN_ENABLE_AVX512)

            target_compile_options(
                ${TARGET}
                PRIVATE
                    /arch:AVX512
            )

        elseif(FIN_ENABLE_AVX2)

            target_compile_options(
                ${TARGET}
                PRIVATE
                    /arch:AVX2
            )

        endif()

    else()

        target_compile_options(
            ${TARGET}
            PRIVATE

                -O3
                -ffast-math
                -Wall
                -Wextra
                -Wpedantic
                -Wshadow
        )

        if(FIN_WARNINGS_AS_ERRORS)

            target_compile_options(
                ${TARGET}
                PRIVATE
                    -Werror
            )

        endif()

        if(FIN_ENABLE_NATIVE)

            target_compile_options(
                ${TARGET}
                PRIVATE
                    -march=native
            )

        elseif(FIN_ENABLE_AVX512)

            target_compile_options(
                ${TARGET}
                PRIVATE
                    -mavx512f
                    -mavx512bw
                    -mavx2
                    -msse4.2
            )

        elseif(FIN_ENABLE_AVX2)

            target_compile_options(
                ${TARGET}
                PRIVATE
                    -mavx2
                    -msse4.2
            )

        endif()

    endif()

endfunction()

# ----------------------------------------------------------------------
# Target Definition
# ----------------------------------------------------------------------

if(FIN_BUILD_SHARED)

    add_library(
        fin_ocr_native
        SHARED
    )

else()

    add_library(
        fin_ocr_native
        STATIC
    )

endif()

# ----------------------------------------------------------------------
# Native Library Sources
# ----------------------------------------------------------------------

target_sources(
    fin_ocr_native
    PRIVATE

        src/ffi_bridge.cpp
        src/matrix_matcher.cpp
        src/ocr_pipeline.cpp
        src/thermal_sensor.cpp
)

# ----------------------------------------------------------------------
# Include Directories
# ----------------------------------------------------------------------

target_include_directories(
    fin_ocr_native
    PUBLIC

        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>

    PRIVATE

        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
        ${PDFIUM_INCLUDE_DIRS}
)

if(FIN_ENABLE_TESSERACT)

    target_include_directories(
        fin_ocr_native
        PRIVATE

            ${TESSERACT_INCLUDE_DIRS}
            ${LEPTONICA_INCLUDE_DIRS}
    )

endif()

# ----------------------------------------------------------------------
# Native Library Dependencies
# ----------------------------------------------------------------------

target_link_libraries(
    fin_ocr_native
    PRIVATE

        ${PDFIUM_LIBRARIES}
)

# ----------------------------------------------------------------------
# Tesseract OCR backend
# ----------------------------------------------------------------------

if(FIN_ENABLE_TESSERACT)

    target_link_libraries(
        fin_ocr_native
        PRIVATE

            PkgConfig::TESSERACT
            PkgConfig::LEPTONICA
    )

    target_compile_definitions(
        fin_ocr_native
        PRIVATE

            FIN_USE_TESSERACT=1
    )

endif()

# ----------------------------------------------------------------------
# Position Independent Code & Visibility
#
# CRITICAL FIX: Ensure C FFI symbols are fully exported in static lib
# ----------------------------------------------------------------------

set_target_properties(
    fin_ocr_native
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
)

# ----------------------------------------------------------------------
# Apply CPU Optimizations to Native Library
# ----------------------------------------------------------------------

fin_apply_cpu_optimizations(
    fin_ocr_native
)

# ----------------------------------------------------------------------
# Link-Time Optimization / IPO
# ----------------------------------------------------------------------

if(FIN_ENABLE_LTO)

    include(CheckIPOSupported)

    check_ipo_supported(
        RESULT ipo_supported
        OUTPUT error_log
    )

    if(ipo_supported)

        set_target_properties(
            fin_ocr_native
            PROPERTIES
                INTERPROCEDURAL_OPTIMIZATION TRUE
        )

        message(
            STATUS
            "[FinOCR] Link Time Optimization (LTO) ENABLED"
        )

    else()

        message(
            WARNING
            "[FinOCR] IPO/LTO requested but not supported "
            "by compiler: ${error_log}"
        )

    endif()

endif()

# ----------------------------------------------------------------------
# Unit Testing
# ----------------------------------------------------------------------

enable_testing()

# ----------------------------------------------------------------------
# GoogleTest
# ----------------------------------------------------------------------

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(
    gtest_force_shared_crt
    ON
    CACHE BOOL
    ""
    FORCE
)

FetchContent_MakeAvailable(
    googletest
)

# ----------------------------------------------------------------------
# Existing Engine Unit Tests
# ----------------------------------------------------------------------

add_executable(
    unit_tests

    tests/test_engine.cpp
)

target_link_libraries(
    unit_tests
    PRIVATE

        fin_ocr_native
        gtest_main
)

fin_apply_cpu_optimizations(
    unit_tests
)

add_test(
    NAME EngineUnitTests
    COMMAND unit_tests
)

# ----------------------------------------------------------------------
# Real-Dataset Integration Tests
# ----------------------------------------------------------------------

add_executable(
    dataset_tests

    tests/test_dataset.cpp
)

target_link_libraries(
    dataset_tests
    PRIVATE

        fin_ocr_native
        gtest_main
)

fin_apply_cpu_optimizations(
    dataset_tests
)

add_test(
    NAME DatasetUnitTests
    COMMAND dataset_tests
)

set_tests_properties(
    DatasetUnitTests
    PROPERTIES
        WORKING_DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

# ----------------------------------------------------------------------
# Tensor Operations Unit Tests
# ----------------------------------------------------------------------

add_executable(
    tensor_ops_tests

    tests/test_tensor_ops.cpp
)

target_link_libraries(
    tensor_ops_tests
    PRIVATE

        fin_ocr_native
        gtest_main
)

fin_apply_cpu_optimizations(
    tensor_ops_tests
)

add_test(
    NAME TensorOpsUnitTests
    COMMAND tensor_ops_tests
)

# ----------------------------------------------------------------------
# Google Benchmark
# ----------------------------------------------------------------------

FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(
    BENCHMARK_ENABLE_TESTING
    OFF
    CACHE BOOL
    ""
    FORCE
)

FetchContent_MakeAvailable(
    googlebenchmark
)

# ----------------------------------------------------------------------
# Existing Synthetic Pipeline Benchmark
# ----------------------------------------------------------------------

add_executable(
    bench_runner

    benchmarks/bench_engine.cpp
)

target_link_libraries(
    bench_runner
    PRIVATE

        fin_ocr_native
        benchmark::benchmark
)

fin_apply_cpu_optimizations(
    bench_runner
)

# ----------------------------------------------------------------------
# Existing Real-Dataset Benchmark
# ----------------------------------------------------------------------

add_executable(
    bench_dataset

    benchmarks/bench_dataset.cpp
)

target_link_libraries(
    bench_dataset
    PRIVATE

        fin_ocr_native
        benchmark::benchmark
)

fin_apply_cpu_optimizations(
    bench_dataset
)

# ----------------------------------------------------------------------
# Tensor Operations Benchmark
# ----------------------------------------------------------------------

add_executable(
    bench_tensor_ops

    benchmarks/bench_tensor_ops.cpp
)

target_link_libraries(
    bench_tensor_ops
    PRIVATE

        fin_ocr_native
        benchmark::benchmark
)

# ----------------------------------------------------------------------
# MatrixMatcher Benchmark
# ----------------------------------------------------------------------

add_executable(
    bench_matrix_matcher

    benchmarks/bench_matrix_matcher.cpp
)

target_link_libraries(
    bench_matrix_matcher
    PRIVATE

        fin_ocr_native
        benchmark::benchmark
)

# ----------------------------------------------------------------------
# Diagnostic Benchmark
# ----------------------------------------------------------------------

add_executable(
    bench_dataset_diagnostic
    benchmarks/bench_dataset_diagnostic.cpp
)

target_link_libraries(
    bench_dataset_diagnostic
    PRIVATE

        fin_ocr_native
)

fin_apply_cpu_optimizations(
    bench_tensor_ops
)

# ----------------------------------------------------------------------
# Build Configuration Summary
# ----------------------------------------------------------------------

message(STATUS "")
message(STATUS "========== FinOCR Build Configuration ==========")
message(STATUS "[FinOCR] C++ Standard      : C++${CMAKE_CXX_STANDARD}")
message(STATUS "[FinOCR] Shared Library    : ${FIN_BUILD_SHARED}")
message(STATUS "[FinOCR] Native CPU        : ${FIN_ENABLE_NATIVE}")
message(STATUS "[FinOCR] AVX2              : ${FIN_ENABLE_AVX2}")
message(STATUS "[FinOCR] AVX-512           : ${FIN_ENABLE_AVX512}")
message(STATUS "[FinOCR] LTO               : ${FIN_ENABLE_LTO}")
message(STATUS "[FinOCR] Tesseract OCR     : ${FIN_ENABLE_TESSERACT}")
message(STATUS "[FinOCR] Warnings as Error : ${FIN_WARNINGS_AS_ERRORS}")
message(STATUS "================================================")
message(STATUS "")
