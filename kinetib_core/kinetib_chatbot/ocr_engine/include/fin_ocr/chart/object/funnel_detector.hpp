#pragma once

#include <cstdint>

#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::object::funnel {

// =============================================================================
// FUNNEL STAGE DETECTOR
// =============================================================================
//
// Detects ordered funnel stages from connected geometry components.
//
// The detector is geometry-only.
// Numeric values and OCR interpretation are handled by later stages.
//
// =============================================================================

void detect_funnel_stages(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    ::fin_ocr::chart::object::ChartObjectSet& result
);

} // namespace fin_ocr::chart::object::funnel
