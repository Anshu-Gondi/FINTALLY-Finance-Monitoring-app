#pragma once

#include <cstdint>
#include <vector>

#include "fin_ocr/chart/axis/axis_types.hpp"

namespace fin_ocr::chart::axis {

// =============================================================================
// HORIZONTAL TICK DETECTION
// =============================================================================
//
// Detects tick positions belonging to a horizontal ChartAxis.
//
// Geometry only.
// OCR label recognition is handled by a separate stage.
//
// =============================================================================

void detect_horizontal_ticks(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    ChartAxis& axis
);

// =============================================================================
// VERTICAL TICK DETECTION
// =============================================================================
//
// Detects tick positions belonging to a vertical ChartAxis.
//
// Geometry only.
// =============================================================================

void detect_vertical_ticks(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    ChartAxis& axis
);

// =============================================================================
// TICK APPEND
// =============================================================================
//
// Adds one tick while enforcing:
//
//   - valid position
//   - maximum tick count
//   - minimum spacing
//   - confidence clamping
//
// =============================================================================

void append_tick(
    std::vector<AxisTick>& ticks,
    int position,
    float confidence = 0.50f
);

} // namespace fin_ocr::chart::axis
