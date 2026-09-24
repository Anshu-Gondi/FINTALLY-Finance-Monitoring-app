#pragma once

#include <cstdint>

#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::object::line {

// =============================================================================
// LINE PATH DETECTOR
// =============================================================================
//
// Detects line-chart paths from the geometry signal inside the chart plot
// region.
//
// Detection is geometry-first. OCR and numeric interpretation are handled by
// later stages.
//
// =============================================================================

void detect_line_paths(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& result
);

} // namespace fin_ocr::chart::object::line
