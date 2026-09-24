#pragma once

#include <cstdint>

namespace fin_ocr::chart {

// =============================================================================
// HORIZONTAL AXIS THICKNESS
// =============================================================================
//
// Estimates the thickness of an already detected horizontal axis.
//
// The estimation is geometry-only and uses horizontal foreground coverage
// around the supplied axis row.
//
// Returns:
//     Estimated odd thickness >= 1.
//
// =============================================================================

[[nodiscard]]
int estimate_horizontal_axis_thickness(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    int y
) noexcept;

// =============================================================================
// VERTICAL AXIS THICKNESS
// =============================================================================
//
// Estimates the thickness of an already detected vertical axis.
//
// The estimation is geometry-only and uses vertical foreground coverage
// around the supplied axis column.
//
// Returns:
//     Estimated odd thickness >= 1.
//
// =============================================================================

[[nodiscard]]
int estimate_vertical_axis_thickness(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    int x
) noexcept;

} // namespace fin_ocr::chart
