#pragma once

#include <cstdint>

#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::object::radial {

// =============================================================================
// RADIAL / PIE / DONUT DETECTOR
// =============================================================================
//
// Detects radial chart structures from their circumference geometry.
//
// The detector currently identifies angularly occupied regions from sampled
// radial signals.
//
// OCR and semantic category interpretation are handled by later stages.
//
// =============================================================================

void detect_radial_slices(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    ::fin_ocr::chart::object::ChartObjectSet& result
);

} // namespace fin_ocr::chart::object::radial
