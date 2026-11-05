# ==============================================================================
# FinOCR Tests
# ==============================================================================

add_executable(
    fin_ocr_tests
        test_dataset.cpp
        test_engine.cpp
        test_tensor_ops.cpp
)

target_link_libraries(
    fin_ocr_tests
    PRIVATE
        fin_ocr_native
        GTest::gtest_main
)

target_compile_features(
    fin_ocr_tests
    PRIVATE
        cxx_std_17
)

# ==============================================================================
# Test fixture / project-root configuration
# ==============================================================================

target_compile_definitions(
    fin_ocr_tests
    PRIVATE
        FIN_OCR_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

# ==============================================================================
# Common compiler configuration
# ==============================================================================

if(COMMAND fin_apply_common_compile_options)
    fin_apply_common_compile_options(
        fin_ocr_tests
    )
endif()

# Keep the test executable position-independent so it remains compatible with
# the native target configuration.
set_target_properties(
    fin_ocr_tests
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

# ==============================================================================
# GoogleTest discovery
# ==============================================================================

include(
    GoogleTest
)

gtest_discover_tests(
    fin_ocr_tests
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
