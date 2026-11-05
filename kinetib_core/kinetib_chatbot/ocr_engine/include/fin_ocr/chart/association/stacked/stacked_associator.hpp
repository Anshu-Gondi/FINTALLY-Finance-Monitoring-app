#pragma once

#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::association::stacked {

// =============================================================================
// STACKED OBJECT ASSOCIATOR
// =============================================================================
//
// Establishes geometric stack relationships between compatible bar/column
// segments.
//
// Does not infer monetary values or percentages.
//
// =============================================================================

void associate_stacked_objects(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& objects
);

} // namespace fin_ocr::chart::association::stacked
