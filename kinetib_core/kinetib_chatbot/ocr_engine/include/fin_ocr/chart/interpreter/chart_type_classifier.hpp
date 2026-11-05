#pragma once

#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/interpreter/interpreter_types.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::interpreter::classifier {

// =============================================================================
// RADIAL TYPE
// =============================================================================

[[nodiscard]]
ChartType classify_radial_type(
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept;

// =============================================================================
// CARTESIAN TYPE
// =============================================================================

[[nodiscard]]
ChartType classify_cartesian_type(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept;

// =============================================================================
// PRIMARY TYPE
// =============================================================================

[[nodiscard]]
ChartType classify_chart_type(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept;

// =============================================================================
// BAR CLASSIFICATION REFINEMENT
// =============================================================================

[[nodiscard]]
ChartType refine_bar_chart_type(
    ChartType current_type,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    bool stacked,
    bool clustered,
    bool percent_stacked,
    bool combo
) noexcept;

} // namespace fin_ocr::chart::interpreter::classifier
