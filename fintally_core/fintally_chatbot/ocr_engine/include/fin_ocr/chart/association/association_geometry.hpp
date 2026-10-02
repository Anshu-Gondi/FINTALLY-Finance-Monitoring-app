#pragma once

#include <utility>

#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::association::geometry {

// =============================================================================
// RECTANGLE
// =============================================================================

[[nodiscard]]
bool valid_rect(
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept;

// =============================================================================
// RECTANGLE CENTER
// =============================================================================

[[nodiscard]]
std::pair<int, int> rect_center(
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept;

// =============================================================================
// CENTER DISTANCE
// =============================================================================

[[nodiscard]]
double center_distance(
    int ax,
    int ay,
    int bx,
    int by
) noexcept;

// =============================================================================
// POINT -> RECTANGLE DISTANCE
// =============================================================================

[[nodiscard]]
double point_to_rect_distance(
    int x,
    int y,
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept;

// =============================================================================
// NORMALIZED DISTANCE
// =============================================================================

[[nodiscard]]
double normalized_distance_score(
    double distance,
    double max_distance
) noexcept;

// =============================================================================
// HORIZONTAL OVERLAP
// =============================================================================

[[nodiscard]]
bool horizontal_overlap(
    int a_min_x,
    int a_max_x,
    int b_min_x,
    int b_max_x
) noexcept;

// =============================================================================
// VERTICAL OVERLAP
// =============================================================================

[[nodiscard]]
bool vertical_overlap(
    int a_min_y,
    int a_max_y,
    int b_min_y,
    int b_max_y
) noexcept;

// =============================================================================
// 1D OVERLAP RATIO
// =============================================================================

[[nodiscard]]
double overlap_ratio_1d(
    int a_min,
    int a_max,
    int b_min,
    int b_max
) noexcept;

// =============================================================================
// OBJECT CENTER
// =============================================================================

[[nodiscard]]
std::pair<int, int> object_center(
    const ::fin_ocr::chart::object::ChartRect& rect
) noexcept;

// =============================================================================
// PATH BOUNDS
// =============================================================================

[[nodiscard]]
::fin_ocr::chart::object::ChartRect path_bounds(
    const ::fin_ocr::chart::object::ChartPath& path
) noexcept;

} // namespace fin_ocr::chart::association::geometry
