#pragma once

#include <cstddef>
#include <cstdint>

namespace fin_ocr::config {

// =============================================================================
// PIXEL / FOREGROUND THRESHOLDS
// =============================================================================

inline constexpr uint8_t DEFAULT_THRESHOLD = 100;
inline constexpr uint8_t CHART_FOREGROUND_THRESHOLD = 20;

// Public MatrixMatcher/document binary representation.
//
// Dark text:
//     grayscale < threshold -> 255
//
// Background:
//     grayscale >= threshold -> 0
inline constexpr uint8_t MATRIX_BINARY_THRESHOLD = 160;

// =============================================================================
// MATRIX MATCHING
// =============================================================================

inline constexpr float MIN_MATCH_SCORE = 0.35f;
inline constexpr float MIN_FOREGROUND_RATIO = 0.005f;
inline constexpr float DIGIT_PRIORITY_MARGIN = 0.08f;

// =============================================================================
// TESSERACT
// =============================================================================

inline constexpr int TESSERACT_ACCEPT_CONFIDENCE = 55;
inline constexpr int TESSERACT_WEAK_CONFIDENCE = 30;

inline constexpr int TESSERACT_SOURCE_DPI = 300;
inline constexpr int TESSERACT_SCALE = 3;
inline constexpr int TESSERACT_PADDING = 4;

inline constexpr std::size_t MAX_TESS_CANDIDATES = 6;

// Maximum raw UTF-8 text returned by a Tesseract document pass.
inline constexpr std::size_t TESSERACT_MAX_TEXT_BYTES = 1'000'000;

// =============================================================================
// LINE RECOGNITION
// =============================================================================

inline constexpr float SPACE_GAP_FACTOR = 0.35f;

// =============================================================================
// CHART RECOGNITION
// =============================================================================

inline constexpr double CHART_MIN_ROW_DENSITY = 0.00010;
inline constexpr double CHART_MIN_BAND_DENSITY = 0.00005;

inline constexpr int CHART_ROW_GAP = 4;
inline constexpr int CHART_MAX_BAND_HEIGHT = 48;
inline constexpr int CHART_VERTICAL_PADDING = 3;

// =============================================================================
// CHART REGION FILTERING
// =============================================================================

// Minimum horizontal extent for a candidate text region.
inline constexpr int CHART_MIN_HORIZONTAL_EXTENT = 3;

// A candidate occupying this much of the chart width is likely chart
// geometry rather than a text label.
inline constexpr double CHART_MAX_TEXT_WIDTH_RATIO = 0.90;

// Dense full-width bands are typically axes, borders, or separators.
inline constexpr double CHART_FULL_WIDTH_DENSE_THRESHOLD = 0.05;

// Sparse full-width bands are typically grid lines / chart geometry.
inline constexpr double CHART_FULL_WIDTH_SPARSE_THRESHOLD = 0.005;

// Minimum absolute number of foreground pixels required for a row
// to participate in chart text-band detection.
//
// This prevents tiny isolated noise from becoming a candidate row.
inline constexpr int CHART_MIN_ROW_FOREGROUND_PIXELS = 2;

// =============================================================================
// GRAYSCALE OCR NORMALIZATION
// =============================================================================
//
// Percentile stretch:
//
//     low percentile  -> black
//     high percentile -> white
//
// This preserves grayscale information instead of binarizing the Tesseract
// input.
//

inline constexpr int GRAY_LOW_PERCENTILE = 1;
inline constexpr int GRAY_HIGH_PERCENTILE = 99;

// =============================================================================
// PDF OCR RASTERIZATION
// =============================================================================

inline constexpr double PDF_OCR_DPI = 300.0;

// Absolute upper bound for the high-resolution OCR raster.
inline constexpr std::size_t PDF_MAX_OCR_PIXELS = 12'000'000;

// Minimum OCR raster dimensions after PDF size limiting.
inline constexpr int PDF_MIN_OCR_WIDTH = 1600;
inline constexpr int PDF_MIN_OCR_HEIGHT = 1200;

} // namespace fin_ocr::config
