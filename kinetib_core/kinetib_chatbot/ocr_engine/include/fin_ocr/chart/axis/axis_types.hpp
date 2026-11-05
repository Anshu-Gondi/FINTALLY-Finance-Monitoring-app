#pragma once

#include <cstdint>
#include <vector>

namespace fin_ocr::chart {

// =============================================================================
// AXIS KIND
// =============================================================================

enum class AxisKind : std::uint8_t {
    UNKNOWN = 0,

    X_AXIS,
    Y_AXIS,

    SECONDARY_X_AXIS,
    SECONDARY_Y_AXIS
};

// =============================================================================
// AXIS TICK
// =============================================================================
//
// Geometry-first representation.
//
// OCR is optional and must not be required for tick detection.
//
// =============================================================================

struct AxisTick {

    int pixel_position = 0;

    int label_min_x = -1;
    int label_max_x = -1;

    int label_min_y = -1;
    int label_max_y = -1;

    double numeric_value = 0.0;

    bool has_numeric_value = false;

    float confidence = 0.0f;
};

// =============================================================================
// CHART AXIS
// =============================================================================

struct ChartAxis {

    AxisKind kind = AxisKind::UNKNOWN;

    int start_x = 0;
    int start_y = 0;

    int end_x = 0;
    int end_y = 0;

    int thickness = 0;

    bool horizontal = false;
    bool vertical = false;

    float confidence = 0.0f;

    std::vector<AxisTick> ticks;
};

} // namespace fin_ocr::chart
