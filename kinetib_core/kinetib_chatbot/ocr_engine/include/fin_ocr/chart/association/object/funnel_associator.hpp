#pragma once

#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::association::object::funnel {

// =============================================================================
// FUNNEL ASSOCIATOR
// =============================================================================

void associate_funnel_objects(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const std::vector<
        ::fin_ocr::chart::association::ChartLabel
    >& labels,
    std::vector<
        ::fin_ocr::chart::association::ChartAssociation
    >& associations
);

} // namespace fin_ocr::chart::association::object::funnel
