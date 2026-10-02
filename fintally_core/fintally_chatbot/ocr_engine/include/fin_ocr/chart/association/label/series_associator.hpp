#pragma once

#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/coordinate/coordinate_system.hpp"

namespace fin_ocr::chart::association::label::series {

// =============================================================================
// SERIES ASSOCIATOR
// =============================================================================
//
// Converts SERIES_LABEL / LEGEND_LABEL instances into ordered ChartSeries
// records.
//
// =============================================================================

void associate_series_labels(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const std::vector<ChartLabel>& labels,
    std::vector<ChartSeries>& series
);

} // namespace fin_ocr::chart::association::label::series
