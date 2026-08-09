cmake_minimum_required(VERSION 3.16)

# Prevent FetchContent timestamp policy warnings (CMP0135)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

project(fin_ocr_native VERSION 1.0.0 LANGUAGES C CXX)

# Force C++17 (compatible with modern C++ standards used in LibTorch and OpenCV)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd, LSP, and static analyzers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------------------
# Build Configuration Options (llama.cpp style build toggles)
# ----------------------------------------------------------------------
option(FIN_BUILD_SHARED        "Build shared library instead of static" OFF)
option(FIN_ENABLE_NATIVE       "Enable -march=native CPU optimizations" ON)
option(FIN_ENABLE_AVX2         "Enable AVX2 vector instructions" ON)
option(FIN_ENABLE_AVX512       "Enable AVX512 vector instructions" OFF)
option(FIN_ENABLE_LTO          "Enable Link Time Optimization (IPO/LTO)" ON)
option(FIN_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors" OFF)

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
        src/ocr_pipeline.cpp
        src/thermal_sensor.cpp
)

target_include_directories(fin_ocr_native
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    # MSVC Flags
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
    # GCC / Clang Flags
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

    # Hardware Architecture Vectorization Flags
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
# Unit Testing & Benchmarks (FetchContent Integration)
# ----------------------------------------------------------------------
enable_testing()
include(FetchContent)

# 1. GoogleTest Setup
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(unit_tests tests/test_engine.cpp)
target_link_libraries(unit_tests PRIVATE fin_ocr_native gtest_main)
add_test(NAME EngineUnitTests COMMAND unit_tests)

# 2. Google Benchmark Setup
FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(bench_runner benchmarks/bench_engine.cpp)
target_link_libraries(bench_runner PRIVATE fin_ocr_native benchmark::benchmark)
