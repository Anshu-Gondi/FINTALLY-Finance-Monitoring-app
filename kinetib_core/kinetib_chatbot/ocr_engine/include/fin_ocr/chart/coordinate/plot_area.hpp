#pragma once

#include "fin_ocr/chart/axis/axis_types.hpp"

namespace fin_ocr::chart::coordinate {

// =============================================================================
// PLOT AREA
// =============================================================================
//
// Primary drawable/data region of a Cartesian chart.
//
// Coordinates are expressed in image-space pixels.
//
// The plot area is derived from already detected primary X/Y axes.
//
// No OCR is required.
//
// =============================================================================

struct PlotArea {

    int min_x = 0;
    int min_y = 0;

    int max_x = 0;
    int max_y = 0;

    float confidence = 0.0f;
};

// =============================================================================
// PLOT AREA DETECTION
// =============================================================================
//
// Derives the primary plot region from:
//
//     x_axis -> horizontal boundary
//     y_axis -> vertical boundary
//
// The function does not perform axis detection itself.
//
// =============================================================================

[[nodiscard]]
PlotArea detect_plot_area(
    const ChartAxis& x_axis,
    const ChartAxis& y_axis,
    int width,
    int height
) noexcept;

} // namespace fin_ocr::chart
