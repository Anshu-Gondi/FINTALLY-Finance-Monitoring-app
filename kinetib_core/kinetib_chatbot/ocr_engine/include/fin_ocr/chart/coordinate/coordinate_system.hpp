#pragma once

#include "fin_ocr/chart/axis/axis_types.hpp"
#include "fin_ocr/chart/coordinate/plot_area.hpp"

namespace fin_ocr::chart {

// =============================================================================
// CHART COORDINATE SYSTEM
// =============================================================================
//
// Represents the coordinate system extracted from a chart.
//
// Primary axes:
//
//     x_axis
//     y_axis
//
// Optional secondary axes:
//
//     secondary_x_axis
//     secondary_y_axis
//
// Plot region:
//
//     plot_area
//
// =============================================================================

struct ChartCoordinateSystem {

    ChartAxis x_axis;
    ChartAxis y_axis;

    ChartAxis secondary_x_axis;
    ChartAxis secondary_y_axis;

    coordinate::PlotArea plot_area;

    bool has_secondary_x_axis = false;
    bool has_secondary_y_axis = false;

    bool valid = false;

    float confidence = 0.0f;
};

} // namespace fin_ocr::chart
