#pragma once

#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::association::dual_axis {

// =============================================================================
// DUAL-AXIS ASSOCIATOR
// =============================================================================
//
// Handles primary / secondary coordinate-system association.
//
// The current object ABI does not expose an explicit primary/secondary axis
// field, so this module must not encode that relationship into unrelated
// fields such as series_index or category_index.
//
// =============================================================================

void associate_dual_axes(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& objects
);

} // namespace fin_ocr::chart::association::dual_axis
