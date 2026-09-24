#pragma once

#include <cstdint>

#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::object {

// =============================================================================
// CHART OBJECT DETECTOR
// =============================================================================
//
// High-level orchestration layer.
//
// Individual object families are implemented by their dedicated detector
// modules:
//
//     bar
//     line
//     area
//     radial
//     scatter
//     waterfall
//     funnel
//     treemap
//
// This class owns orchestration only.
// =============================================================================

class ChartObjectDetector {
public:

    ChartObjectDetector() = default;

    ~ChartObjectDetector() = default;

    ChartObjectDetector(
        const ChartObjectDetector&
    ) = default;

    ChartObjectDetector& operator=(
        const ChartObjectDetector&
    ) = default;

    ChartObjectDetector(
        ChartObjectDetector&&
    ) noexcept = default;

    ChartObjectDetector& operator=(
        ChartObjectDetector&&
    ) noexcept = default;

    // =========================================================================
    // DETECT
    // =========================================================================

    [[nodiscard]]
    ChartObjectSet detect(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        const ::fin_ocr::chart::ChartCoordinateSystem& coordinates
    ) const;
};

} // namespace fin_ocr::chart::object
