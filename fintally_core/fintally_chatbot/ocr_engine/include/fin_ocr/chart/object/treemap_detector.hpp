#pragma once

#include <cstdint>

#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::object::treemap {

// =============================================================================
// TREEMAP DETECTOR
// =============================================================================

void detect_treemap_regions(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    ::fin_ocr::chart::object::ChartObjectSet& result
);

} // namespace fin_ocr::chart::object::treemap
