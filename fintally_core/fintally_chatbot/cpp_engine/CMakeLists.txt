cmake_minimum_required(VERSION 3.16)

# Prevent FetchContent timestamp policy warnings (CMP0135)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

project(fin_ocr_native VERSION 1.0.0 LANGUAGES C CXX)

# Force C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd, LSP, and static analyzers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------------------
# Build Configuration Options
# ----------------------------------------------------------------------
option(FIN_BUILD_SHARED        "Build shared library instead of static" OFF)
option(FIN_ENABLE_NATIVE       "Enable -march=native CPU optimizations" ON)
option(FIN_ENABLE_AVX2         "Enable AVX2 vector instructions" ON)
option(FIN_ENABLE_AVX512       "Enable AVX512 vector instructions" OFF)
option(FIN_ENABLE_LTO          "Enable Link Time Optimization (IPO/LTO)" ON)
option(FIN_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors" OFF)

include(FetchContent)

# ----------------------------------------------------------------------
# External Dependencies (stb & PDFium)
# ----------------------------------------------------------------------

# 1. Fetch stb_image (Header-only library for image decoding)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

# 2. PDFium Integration (PDF rasterization engine)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PDFIUM QUIET pdfium)
endif()

if(NOT PDFIUM_FOUND)
    set(PDFIUM_ROOT_DIR "" CACHE PATH "Path to PDFium installation directory")
    if(PDFIUM_ROOT_DIR)
        set(PDFIUM_INCLUDE_DIRS "${PDFIUM_ROOT_DIR}/include")
        if(EXISTS "${PDFIUM_ROOT_DIR}/lib/libpdfium.so")
            set(PDFIUM_LIBRARIES "${PDFIUM_ROOT_DIR}/lib/libpdfium.so")
        else()
            set(PDFIUM_LIBRARIES "${PDFIUM_ROOT_DIR}/lib/pdfium.lib")
        endif()
    else()
        message(STATUS "[FinOCR] PDFium not found locally. Fetching prebuilt PDFium binaries...")
        FetchContent_Declare(
            pdfium_binaries
            URL https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F6531/pdfium-linux-x64.tgz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(pdfium_binaries)

        set(PDFIUM_INCLUDE_DIRS "${pdfium_binaries_SOURCE_DIR}/include")
        set(PDFIUM_LIBRARIES "${pdfium_binaries_SOURCE_DIR}/lib/libpdfium.so")
    endif()
endif()

# ----------------------------------------------------------------------
# Target Definition
# ----------------------------------------------------------------------
if(FIN_BUILD_SHARED)
    add_library(fin_ocr_native SHARED)
else()
    add_library(fin_ocr_native STATIC)
endif()

target_sources(fin_ocr_native
    PRIVATE
        src/ffi_bridge.cpp
        src/matrix_matcher.cpp
        src/ocr_pipeline.cpp
        src/thermal_sensor.cpp
)

target_include_directories(fin_ocr_native
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
        ${PDFIUM_INCLUDE_DIRS}
)

target_link_libraries(fin_ocr_native
    PRIVATE
        ${PDFIUM_LIBRARIES}
)

# Enable Position Independent Code (-fPIC) for safe Rust/FFI static linking
set_target_properties(fin_ocr_native PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ----------------------------------------------------------------------
# Link-Time Optimization (LTO / IPO)
# ----------------------------------------------------------------------
if(FIN_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT error_log)
    if(ipo_supported)
        set_target_properties(fin_ocr_native PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "[FinOCR] Link Time Optimization (LTO) ENABLED")
    else()
        message(WARNING "[FinOCR] IPO/LTO requested but not supported by compiler: ${error_log}")
    endif()
endif()

# ----------------------------------------------------------------------
# Target-Based Compiler & SIMD Optimization Flags
# ----------------------------------------------------------------------
if(MSVC)
    target_compile_options(fin_ocr_native PRIVATE
        /O2                 # Max speed optimization
        /fp:fast            # Fast floating point behavior
        /W4                 # High warning level
        /permissive-         # Strict C++ conformance
    )

    if(FIN_WARNINGS_AS_ERRORS)
        target_compile_options(fin_ocr_native PRIVATE /WX)
    endif()

    if(FIN_ENABLE_AVX512)
        target_compile_options(fin_ocr_native PRIVATE /arch:AVX512)
    elseif(FIN_ENABLE_AVX2)
        target_compile_options(fin_ocr_native PRIVATE /arch:AVX2)
    endif()

else()
    target_compile_options(fin_ocr_native PRIVATE
        -O3
        -ffast-math
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -fvisibility=hidden  # Hide internal C++ symbols for cleaner C FFI binary
    )

    if(FIN_WARNINGS_AS_ERRORS)
        target_compile_options(fin_ocr_native PRIVATE -Werror)
    endif()

    if(FIN_ENABLE_NATIVE)
        target_compile_options(fin_ocr_native PRIVATE -march=native)
    else()
        if(FIN_ENABLE_AVX512)
            target_compile_options(fin_ocr_native PRIVATE -mavx512f -mavx512bw -mavx2 -msse4.2)
        elseif(FIN_ENABLE_AVX2)
            target_compile_options(fin_ocr_native PRIVATE -mavx2 -msse4.2)
        endif()
    endif()
endif()

# ----------------------------------------------------------------------
# Unit Testing & Benchmarks
# ----------------------------------------------------------------------
enable_testing()

# 1. GoogleTest Setup
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Existing Engine Unit Tests
add_executable(unit_tests tests/test_engine.cpp)
target_link_libraries(unit_tests PRIVATE fin_ocr_native gtest_main)
add_test(NAME EngineUnitTests COMMAND unit_tests)

# Real-Dataset Integration Tests
add_executable(dataset_tests tests/test_dataset.cpp)
target_link_libraries(dataset_tests PRIVATE fin_ocr_native gtest_main)
add_test(NAME DatasetUnitTests COMMAND dataset_tests)
set_tests_properties(DatasetUnitTests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# 2. Google Benchmark Setup
FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# Synthetic Pipeline Benchmarks
add_executable(bench_runner benchmarks/bench_engine.cpp)
target_link_libraries(bench_runner PRIVATE fin_ocr_native benchmark::benchmark)

# Real-Dataset Benchmarks
add_executable(bench_dataset benchmarks/bench_dataset.cpp)
target_link_libraries(bench_dataset PRIVATE fin_ocr_native benchmark::benchmark)
