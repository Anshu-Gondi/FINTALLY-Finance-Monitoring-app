include(FetchContent)

# ==============================================================================
# External dependency discovery / acquisition
# ==============================================================================

# ------------------------------------------------------------------------------
# 1. pkg-config
#
# Used for:
#   - Tesseract
#   - Leptonica
#   - PDFium, when installed system-wide
# ------------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)

# ------------------------------------------------------------------------------
# 2. Tesseract + Leptonica
# ------------------------------------------------------------------------------

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

# ------------------------------------------------------------------------------
# 3. stb_image
# ------------------------------------------------------------------------------

FetchContent_Declare(
    stb
    GIT_REPOSITORY
        https://github.com/nothings/stb.git
    GIT_TAG
        master
)

FetchContent_MakeAvailable(stb)

# ------------------------------------------------------------------------------
# 4. PDFium
#
# Search order:
#
#   1. pkg-config
#   2. FIN_PDFIUM_ROOT_DIR
#   3. FetchContent prebuilt Linux x64 PDFium
# ------------------------------------------------------------------------------

pkg_check_modules(
    PDFIUM
    QUIET
    pdfium
)

if(PDFIUM_FOUND)

    message(
        STATUS
        "[FinOCR] PDFium found through pkg-config"
    )

else()

    set(
        FIN_PDFIUM_ROOT_DIR
        ""
        CACHE PATH
        "Path to PDFium installation directory"
    )

    if(FIN_PDFIUM_ROOT_DIR)

        message(
            STATUS
            "[FinOCR] Using PDFium from FIN_PDFIUM_ROOT_DIR="
            "${FIN_PDFIUM_ROOT_DIR}"
        )

        set(
            PDFIUM_INCLUDE_DIRS
            "${FIN_PDFIUM_ROOT_DIR}/include"
        )

        if(
            EXISTS
            "${FIN_PDFIUM_ROOT_DIR}/lib/libpdfium.so"
        )

            set(
                PDFIUM_LIBRARIES
                "${FIN_PDFIUM_ROOT_DIR}/lib/libpdfium.so"
            )

        elseif(
            EXISTS
            "${FIN_PDFIUM_ROOT_DIR}/lib/pdfium.lib"
        )

            set(
                PDFIUM_LIBRARIES
                "${FIN_PDFIUM_ROOT_DIR}/lib/pdfium.lib"
            )

        else()

            message(
                FATAL_ERROR
                "[FinOCR] PDFium root specified but no supported "
                "PDFium library was found."
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
            URL
                https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F6531/pdfium-linux-x64.tgz
            DOWNLOAD_EXTRACT_TIMESTAMP
                TRUE
        )

        FetchContent_MakeAvailable(
            pdfium_binaries
        )

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

# ==============================================================================
# 5. GoogleTest
#
# Test-only dependency.
#
# Provides:
#
#     GTest::gtest
#     GTest::gtest_main
# ==============================================================================

option(
    FIN_BUILD_TESTS
    "Build FinOCR unit and integration tests"
    ON
)

if(FIN_BUILD_TESTS)

    message(
        STATUS
        "[FinOCR] GoogleTest ENABLED"
    )

    FetchContent_Declare(
        googletest
        URL
            https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP
            TRUE
    )

    # Keep MSVC runtime handling compatible with shared CRT builds.
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

else()

    message(
        STATUS
        "[FinOCR] GoogleTest DISABLED"
    )

endif()

# ==============================================================================
# 6. Google Benchmark
#
# Benchmark-only dependency.
#
# Provides:
#
#     benchmark::benchmark
# ==============================================================================

option(
    FIN_BUILD_BENCHMARKS
    "Build FinOCR benchmarks"
    ON
)

if(FIN_BUILD_BENCHMARKS)

    message(
        STATUS
        "[FinOCR] Google Benchmark ENABLED"
    )

    FetchContent_Declare(
        googlebenchmark
        URL
            https://github.com/google/benchmark/archive/refs/tags/v1.8.3.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP
            TRUE
    )

    # We do not need benchmark's own test suite.
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

else()

    message(
        STATUS
        "[FinOCR] Google Benchmark DISABLED"
    )

endif()

# ==============================================================================
# Dependency application
# ==============================================================================

function(fin_ocr_link_dependencies TARGET)

    if(NOT TARGET "${TARGET}")

        message(
            FATAL_ERROR
            "[FinOCR] Cannot link dependencies: target '${TARGET}' "
            "does not exist."
        )

    endif()

    # --------------------------------------------------------------------------
    # stb
    # --------------------------------------------------------------------------

    target_include_directories(
        "${TARGET}"
        PRIVATE
            "${stb_SOURCE_DIR}"
    )

    # --------------------------------------------------------------------------
    # PDFium
    # --------------------------------------------------------------------------

    target_include_directories(
        "${TARGET}"
        PRIVATE
            ${PDFIUM_INCLUDE_DIRS}
    )

    target_link_libraries(
        "${TARGET}"
        PRIVATE
            ${PDFIUM_LIBRARIES}
    )

    # --------------------------------------------------------------------------
    # Tesseract
    # --------------------------------------------------------------------------

    if(FIN_ENABLE_TESSERACT)

        target_include_directories(
            "${TARGET}"
            PRIVATE
                ${TESSERACT_INCLUDE_DIRS}
                ${LEPTONICA_INCLUDE_DIRS}
        )

        target_link_libraries(
            "${TARGET}"
            PRIVATE
                PkgConfig::TESSERACT
                PkgConfig::LEPTONICA
        )

        target_compile_definitions(
            "${TARGET}"
            PRIVATE
                FIN_USE_TESSERACT=1
        )

    endif()

endfunction()

# ==============================================================================
# Dependency summary
# ==============================================================================

message(STATUS "")
message(STATUS "============================================================")
message(STATUS " FinOCR External Dependencies")
message(STATUS "============================================================")
message(STATUS "[FinOCR] Tesseract       : ${FIN_ENABLE_TESSERACT}")
message(STATUS "[FinOCR] GoogleTest       : ${FIN_BUILD_TESTS}")
message(STATUS "[FinOCR] GoogleBenchmark  : ${FIN_BUILD_BENCHMARKS}")
message(STATUS "[FinOCR] PDFium           : ${PDFIUM_LIBRARIES}")
message(STATUS "[FinOCR] stb              : ${stb_SOURCE_DIR}")
message(STATUS "============================================================")
message(STATUS "")
