#pragma once

#include <utility>

#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::interpreter::geometry {

// =============================================================================
// RECTANGLE VALIDATION
// =============================================================================

[[nodiscard]]
bool valid_rect(
    const ::fin_ocr::chart::object::ChartRect& rect
) noexcept;

// =============================================================================
// RECTANGLE DIMENSIONS
// =============================================================================

[[nodiscard]]
int rect_width(
    const ::fin_ocr::chart::object::ChartRect& rect
) noexcept;

[[nodiscard]]
int rect_height(
    const ::fin_ocr::chart::object::ChartRect& rect
) noexcept;

// =============================================================================
// RECTANGLE CENTER
// =============================================================================

[[nodiscard]]
std::pair<int, int> rect_center(
    const ::fin_ocr::chart::object::ChartRect& rect
) noexcept;

// =============================================================================
// NUMERIC VALIDATION
// =============================================================================

[[nodiscard]]
bool finite_value(
    double value
) noexcept;

// =============================================================================
// PLOT CONTAINMENT
// =============================================================================

[[nodiscard]]
bool inside_plot(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    int x,
    int y
) noexcept;

// =============================================================================
// BOUNDARY PROXIMITY
// =============================================================================

[[nodiscard]]
bool near_same_x(
    const ::fin_ocr::chart::object::ChartRect& a,
    const ::fin_ocr::chart::object::ChartRect& b
) noexcept;

[[nodiscard]]
bool near_same_y(
    const ::fin_ocr::chart::object::ChartRect& a,
    const ::fin_ocr::chart::object::ChartRect& b
) noexcept;

// =============================================================================
// BAR ORIENTATION
// =============================================================================

[[nodiscard]]
bool is_vertical_bar(
    const ::fin_ocr::chart::object::BarSegment& bar
) noexcept;

// =============================================================================
// STACK COMPATIBILITY
// =============================================================================

[[nodiscard]]
bool stack_compatible(
    const ::fin_ocr::chart::object::BarSegment& a,
    const ::fin_ocr::chart::object::BarSegment& b
) noexcept;

} // namespace fin_ocr::chart::interpreter::geometry
