#pragma once

#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/coordinate/coordinate_system.hpp"

namespace fin_ocr::chart::association::label {

// =============================================================================
// LABEL CLASSIFIER
// =============================================================================
//
// Classifies already-recognized OCR labels into semantic label categories.
//
// OCR recognition itself is outside this module.
//
// =============================================================================

void classify_labels(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    std::vector<ChartLabel>& labels
);

} // namespace fin_ocr::chart::association::label
