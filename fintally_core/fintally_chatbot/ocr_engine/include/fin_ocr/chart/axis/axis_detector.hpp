#pragma once

#include <cstdint>

#include "fin_ocr/chart/axis/axis_types.hpp"
#include "fin_ocr/chart/coordinate/coordinate_system.hpp"

namespace fin_ocr::chart {

// =============================================================================
// CHART AXIS DETECTOR
// =============================================================================
//
// Detects the primary coordinate system of a chart image.
//
// Responsibilities:
//
//   1. Detect horizontal X axis.
//   2. Detect vertical Y axis.
//   3. Assemble the coordinate system.
//
// Plot-area geometry is implemented by coordinate::plot_area.
//
// Tick detection is delegated to AxisTickDetector.
//
// =============================================================================

class ChartAxisDetector {
public:

    ChartAxisDetector() = default;

    ~ChartAxisDetector() = default;

    ChartAxisDetector(
        const ChartAxisDetector&
    ) = default;

    ChartAxisDetector& operator=(
        const ChartAxisDetector&
    ) = default;

    ChartAxisDetector(
        ChartAxisDetector&&
    ) noexcept = default;

    ChartAxisDetector& operator=(
        ChartAxisDetector&&
    ) noexcept = default;

    // =========================================================================
    // DETECT
    // =========================================================================

    [[nodiscard]]
    ChartCoordinateSystem detect(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels
    ) const;

private:

    // =========================================================================
    // PRIMARY AXIS DETECTION
    // =========================================================================

    [[nodiscard]]
    ChartAxis detect_horizontal_axis(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels
    ) const;

    [[nodiscard]]
    ChartAxis detect_vertical_axis(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels
    ) const;
};

} // namespace fin_ocr::chart
