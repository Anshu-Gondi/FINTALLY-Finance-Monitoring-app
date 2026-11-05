#pragma once

#include <cstdint>

#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::object::bar {

// =============================================================================
// BAR / COLUMN DETECTOR
// =============================================================================
//
// Detects rectangular bar and column components.
//
// The detector is geometry-first.
// Numeric values are inferred only when the coordinate system contains
// sufficient numeric tick information.
//
// =============================================================================

void detect_bars_and_columns(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& result
);

} // namespace fin_ocr::chart::object::bar
