cmake_minimum_required(VERSION 3.15)
project(fin_ocr_native CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------------------
# Aggressive Optimizations (llama.cpp style)
# ----------------------------------------------------------------------
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -ffast-math")

# Disable heavy C++ features to save RAM and binary size
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")

# Enable Link Time Optimization (LTO)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "IPO / LTO enabled for maximum performance")
endif()

# ----------------------------------------------------------------------
# Target Definition
# ----------------------------------------------------------------------
# We build a STATIC library because Rust (via cc/bindgen) will link it directly
add_library(fin_ocr_native STATIC
    src/ffi_bridge.cpp
    src/ocr_pipeline.cpp
)

target_include_directories(fin_ocr_native PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
