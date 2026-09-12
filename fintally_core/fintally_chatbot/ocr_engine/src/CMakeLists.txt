# ==============================================================================
# FinOCR Native Source Graph
# ==============================================================================

add_library(
    fin_ocr_native
    STATIC

    # --------------------------------------------------------------------------
    # Public/native C ABI boundary
    # --------------------------------------------------------------------------
    ffi_bridge.cpp

    # --------------------------------------------------------------------------
    # Public MatrixMatcher facade
    #
    # This is NOT the old monolithic implementation.
    #
    # It delegates to:
    #   GlyphMatcher
    #   LineRecognizer
    #   TesseractRecognizer
    #   ChartLabelRecognizer
    # --------------------------------------------------------------------------
    matrix_matcher.cpp

    # --------------------------------------------------------------------------
    # Thermal monitoring
    # --------------------------------------------------------------------------
    thermal_sensor.cpp
)

# ==============================================================================
# PUBLIC INCLUDE PATH
# ==============================================================================

target_include_directories(
    fin_ocr_native
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

# Internal source headers are allowed to include each other through
# fin_ocr/... paths, while implementation-only headers may still live under src.
target_include_directories(
    fin_ocr_native
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

# ==============================================================================
# C++ STANDARD
# ==============================================================================

target_compile_features(
    fin_ocr_native
    PUBLIC
        cxx_std_17
)

# ==============================================================================
# SUBSYSTEMS
# ==============================================================================
#
# Each directory owns its own source files.
#
#     core
#         safe arithmetic
#         buffer utilities
#
#     image
#         grayscale
#         luminance
#         binary masks
#         resize
#         contrast normalization
#
#     matrix
#         glyph matcher
#         glyph normalizer
#         glyph scorer
#         glyph templates
#
#     segmentation
#         connected components
#
#     tesseract
#         engine
#         preprocessing
#         line/document recognition helpers
#
#     line
#         hybrid line orchestration
#
#     chart
#         chart color isolation
#         chart label recognition
#
#     document
#         document-level hybrid OCR
#
#     pdf
#         page metadata
#         PDF rasterization
#
#     pipeline
#         top-level VisionPipeline orchestration
#
# ==============================================================================

add_subdirectory(core)

add_subdirectory(image)

add_subdirectory(matrix)

add_subdirectory(segmentation)

add_subdirectory(tesseract)

add_subdirectory(line)

add_subdirectory(chart)

add_subdirectory(document)

add_subdirectory(pdf)

add_subdirectory(pipeline)

# ==============================================================================
# EXTERNAL DEPENDENCIES
# ==============================================================================
#
# Dependencies.cmake owns discovery.
#
# This function attaches the discovered targets/libraries to the native
# production target.
# ==============================================================================

fin_ocr_link_dependencies(
    fin_ocr_native
)
