#pragma once

#include <cstdint>

#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::object::area {

// =============================================================================
// AREA REGION DETECTOR
// =============================================================================
//
// Detects large filled regions inside the chart plot area.
//
// The detector is geometry-first and does not perform OCR or numeric
// interpretation.
//
// =============================================================================

void detect_area_regions(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& result
);

} // namespace fin_ocr::chart::object::area
