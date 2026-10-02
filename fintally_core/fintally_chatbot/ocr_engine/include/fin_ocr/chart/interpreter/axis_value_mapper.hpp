#pragma once

#include "fin_ocr/chart/axis/axis_types.hpp"
#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::interpreter::axis {

// =============================================================================
// NUMERIC TICK VALIDATION
// =============================================================================

[[nodiscard]]
bool has_numeric_ticks(
    const ::fin_ocr::chart::ChartAxis& axis
) noexcept;

// =============================================================================
// AXIS INTERPOLATION
// =============================================================================

[[nodiscard]]
bool interpolate_axis_value(
    const ::fin_ocr::chart::ChartAxis& axis,
    int pixel,
    double& value
) noexcept;

// =============================================================================
// PIXEL -> VALUE
// =============================================================================

[[nodiscard]]
double pixel_to_value(
    const ::fin_ocr::chart::ChartAxis& axis,
    int pixel
) noexcept;

// =============================================================================
// SCALE
// =============================================================================

[[nodiscard]]
double estimate_scale(
    const ::fin_ocr::chart::ChartAxis& axis
) noexcept;

// =============================================================================
// PATH POINT MAPPING
// =============================================================================

[[nodiscard]]
bool path_point_values(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartPathPoint& point,
    double& x_value,
    double& y_value
) noexcept;

} // namespace fin_ocr::chart::interpreter::axis
