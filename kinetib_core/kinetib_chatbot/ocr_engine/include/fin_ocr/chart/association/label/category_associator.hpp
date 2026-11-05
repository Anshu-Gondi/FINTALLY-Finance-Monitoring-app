#pragma once

#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/coordinate/coordinate_system.hpp"

namespace fin_ocr::chart::association::label::category {

// =============================================================================
// CATEGORY ASSOCIATOR
// =============================================================================
//
// Associates X-axis labels with ordered chart categories.
//
// =============================================================================

void associate_axis_labels(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    std::vector<ChartLabel>& labels,
    std::vector<ChartCategory>& categories
);

} // namespace fin_ocr::chart::association::label::category
