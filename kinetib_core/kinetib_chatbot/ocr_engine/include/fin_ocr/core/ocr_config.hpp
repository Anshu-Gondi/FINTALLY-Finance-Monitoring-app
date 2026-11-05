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
//
// General chart OCR/text-band detection.
//

inline constexpr double CHART_MIN_ROW_DENSITY = 0.00010;
inline constexpr double CHART_MIN_BAND_DENSITY = 0.00005;

inline constexpr int CHART_ROW_GAP = 4;
inline constexpr int CHART_MAX_BAND_HEIGHT = 48;
inline constexpr int CHART_VERTICAL_PADDING = 3;

// Maximum number of OCR-recognized chart labels retained.
inline constexpr std::size_t CHART_MAX_LABELS = 2048;

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
// CHART LABEL CANDIDATE FILTERING
// =============================================================================
//
// Deterministic geometry filters applied before LineRecognizer.
//
// These parameters are intentionally separate from:
//
//     - chart semantic classification
//     - axis/series association
//     - object detection
//
// They are used to suppress:
//
//     - plot-wide chart lines
//     - grid lines
//     - axis segments
//     - borders
//     - extremely small noise
//     - excessively tall connected structures
//
// They do NOT determine whether a recognized label is:
//
//     X_AXIS_LABEL
//     Y_AXIS_LABEL
//     SERIES_LABEL
//     TITLE
//     DATA_LABEL
//
// That semantic decision remains the responsibility of ChartAssociator.
// =============================================================================

// Minimum candidate width in pixels.
inline constexpr int CHART_LABEL_MIN_WIDTH = 2;

// Minimum candidate height in pixels.
inline constexpr int CHART_LABEL_MIN_HEIGHT = 4;

// Maximum candidate height in pixels.
inline constexpr int CHART_LABEL_MAX_HEIGHT = 48;

// Maximum width passed to one OCR candidate.
inline constexpr int CHART_LABEL_MAX_CANDIDATE_WIDTH = 320;

// A candidate wider than this fraction of the image is considered
// suspicious chart geometry rather than an individual text label.
inline constexpr double CHART_LABEL_MAX_WIDTH_RATIO = 0.25;

// A candidate taller than this fraction of the image is considered
// non-text geometry.
inline constexpr double CHART_LABEL_MAX_HEIGHT_RATIO = 0.15;

// Minimum foreground density inside an OCR candidate.
inline constexpr double CHART_LABEL_MIN_DENSITY = 0.015;

// Minimum ratio of vertical extent containing actual foreground.
inline constexpr double CHART_LABEL_MIN_VERTICAL_INK_RATIO = 0.10;

// Candidates dominated by horizontal line geometry can be rejected.
inline constexpr double CHART_LABEL_MAX_HORIZONTAL_LINE_RATIO = 0.65;

// Candidates dominated by vertical line geometry can be rejected.
inline constexpr double CHART_LABEL_MAX_VERTICAL_LINE_RATIO = 0.65;

// Maximum gap used when joining neighboring glyph fragments.
inline constexpr int CHART_LABEL_MAX_HORIZONTAL_GAP = 12;

// Minimum width of a component before it can become an OCR candidate.
inline constexpr int CHART_LABEL_MIN_COMPONENT_WIDTH = 2;

// Safety limit for intermediate candidate/component generation.
inline constexpr int CHART_LABEL_MAX_COMPONENTS = 512;

// =============================================================================
// CHART AXIS DETECTION
// =============================================================================
//
// Deterministic geometry parameters used by ChartAxisDetector.
//
// These values control:
//
//     - minimum axis length
//     - horizontal / vertical coverage
//     - foreground density required for an axis candidate
//     - tick-search radius
//     - minimum distance between detected ticks
//     - maximum number of ticks retained
//     - image-edge exclusion
//
// They are intentionally separate from text-band/OCR thresholds.
//

inline constexpr int CHART_MIN_AXIS_LENGTH_PIXELS = 40;

inline constexpr int CHART_AXIS_SEARCH_THICKNESS = 3;

inline constexpr double CHART_MIN_HORIZONTAL_AXIS_COVERAGE = 0.20;

inline constexpr double CHART_MIN_VERTICAL_AXIS_COVERAGE = 0.20;

inline constexpr double CHART_MIN_AXIS_DENSITY = 0.08;

inline constexpr int CHART_MAX_TICK_SEARCH_DISTANCE = 12;

inline constexpr int CHART_MIN_TICK_SPACING = 8;

inline constexpr int CHART_MAX_TICKS = 256;

inline constexpr int CHART_AXIS_EDGE_MARGIN = 2;

// =============================================================================
// CHART OBJECT DETECTION
// =============================================================================
//
// Deterministic geometry parameters used by ChartObjectDetector.
//
// These values control:
//
//     - minimum detected rectangle dimensions
//     - maximum number of retained chart objects
//     - component-density filtering
//     - bar/column aspect-ratio classification
//     - line detection sensitivity
//     - connected-component safety limit
//     - radial chart sampling
//     - minimum radial occupancy
//     - minimum funnel stage count
//     - minimum treemap rectangle dimensions
//
// These parameters describe chart geometry, not OCR text recognition.
// =============================================================================

inline constexpr int CHART_OBJECT_MIN_RECT_WIDTH = 4;

inline constexpr int CHART_OBJECT_MIN_RECT_HEIGHT = 4;

inline constexpr std::size_t CHART_OBJECT_MAX_OBJECTS = 4096;

inline constexpr double CHART_OBJECT_MIN_DENSITY = 0.04;

inline constexpr double CHART_OBJECT_MIN_BAR_ASPECT = 0.15;

inline constexpr double CHART_OBJECT_MAX_BAR_ASPECT = 20.0;

inline constexpr double CHART_OBJECT_MIN_LINE_DENSITY = 0.015;

inline constexpr int CHART_OBJECT_MAX_COMPONENTS = 4096;

inline constexpr int CHART_OBJECT_RADIAL_MIN_RADIUS = 12;

inline constexpr int CHART_OBJECT_RADIAL_SAMPLE_COUNT = 360;

inline constexpr double CHART_OBJECT_RADIAL_MIN_COVERAGE = 0.20;

inline constexpr int CHART_OBJECT_FUNNEL_MIN_STAGES = 2;

inline constexpr int CHART_OBJECT_TREEMAP_MIN_RECT_SIZE = 12;

// =============================================================================
// CHART LABEL ASSOCIATION
// =============================================================================
//
// Deterministic geometry/semantic association parameters.
//
// These values control:
//
//     - maximum distance from axis labels to their axes
//     - maximum distance from labels to chart objects
//     - maximum distance from labels to series/legend structures
//     - minimum accepted association confidence
//     - global association result limits
//     - category and series safety limits
//
// They do not perform OCR and do not affect pixel recognition.
// =============================================================================

inline constexpr double CHART_LABEL_MAX_AXIS_DISTANCE = 48.0;

inline constexpr double CHART_LABEL_MAX_OBJECT_DISTANCE = 96.0;

inline constexpr double CHART_LABEL_MAX_SERIES_DISTANCE = 96.0;

inline constexpr double CHART_LABEL_MIN_ASSOCIATION_CONFIDENCE = 0.10;

inline constexpr std::size_t CHART_MAX_ASSOCIATIONS = 8192;

inline constexpr std::size_t CHART_MAX_CATEGORIES = 2048;

inline constexpr std::size_t CHART_MAX_SERIES = 256;

// Geometric scoring weights.

inline constexpr double CHART_LABEL_HORIZONTAL_OVERLAP_WEIGHT = 0.65;

inline constexpr double CHART_LABEL_VERTICAL_OVERLAP_WEIGHT = 0.65;

inline constexpr double CHART_LABEL_DISTANCE_WEIGHT = 0.35;

// =============================================================================
// CHART INTERPRETER
// =============================================================================
//
// Deterministic semantic-analysis parameters used by ChartInterpreter.
//
// These values control:
//
//     - pixel-to-value scale validation
//     - stack/cluster geometry tolerance
//     - minimum number of numeric axis ticks
//     - minimum line-path samples for combo detection
//     - minimum confidence retained by the interpreter
//
// These parameters do not perform OCR and do not alter image processing.
// =============================================================================

inline constexpr double CHART_INTERPRETER_PI =
    3.141592653589793238462643383279502884;

inline constexpr double CHART_INTERPRETER_MIN_SCALE_PIXELS =
    1.0;

inline constexpr double CHART_INTERPRETER_MIN_NUMERIC_AXIS_TICKS =
    2.0;

inline constexpr double CHART_INTERPRETER_STACK_X_TOLERANCE =
    4.0;

inline constexpr double CHART_INTERPRETER_STACK_Y_TOLERANCE =
    4.0;

inline constexpr double CHART_INTERPRETER_CLUSTER_X_TOLERANCE =
    12.0;

inline constexpr double CHART_INTERPRETER_CLUSTER_Y_TOLERANCE =
    12.0;

inline constexpr double CHART_INTERPRETER_COMBO_MIN_LINE_POINTS =
    3.0;

inline constexpr double CHART_INTERPRETER_MIN_CONFIDENCE =
    0.01;

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
