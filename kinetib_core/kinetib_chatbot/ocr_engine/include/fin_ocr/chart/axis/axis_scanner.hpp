#pragma once

#include <cstdint>

#include "fin_ocr/chart/axis/axis_types.hpp"

namespace fin_ocr::chart::axis {

// =============================================================================
// AXIS SCANNER
// =============================================================================
//
// Detects the primary horizontal and vertical axes of a chart.
//
// Responsibilities:
//
//   - scan candidate horizontal rows
//   - scan candidate vertical columns
//   - measure foreground span statistics
//   - apply axis acceptance thresholds
//   - calculate candidate scores
//   - select the strongest candidate
//   - construct ChartAxis geometry
//
// Non-responsibilities:
//
//   - tick detection
//   - axis thickness estimation
//   - plot-area construction
//   - OCR
//   - chart interpretation
//
// =============================================================================

class AxisScanner {
public:

    AxisScanner() = default;

    // =========================================================================
    // HORIZONTAL AXIS
    // =========================================================================

    [[nodiscard]]
    ChartAxis detect_horizontal(
        const std::uint8_t* image,
        int width,
        int height,
        int channels
    ) const;

    // =========================================================================
    // VERTICAL AXIS
    // =========================================================================

    [[nodiscard]]
    ChartAxis detect_vertical(
        const std::uint8_t* image,
        int width,
        int height,
        int channels
    ) const;
};

} // namespace fin_ocr::chart::axis
