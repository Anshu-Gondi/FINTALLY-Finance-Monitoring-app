#pragma once

#include <cstdint>
#include <vector>

#include "fin_ocr/chart/label/label_types.hpp"

namespace fin_ocr::chart::label::y_axis {

// =============================================================================
// Y-AXIS FRAGMENT CONSOLIDATION
// =============================================================================

[[nodiscard]]
std::vector<TextCandidate> consolidate_y_axis_fragments(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const std::vector<TextCandidate>& fragments,
    std::uint8_t minimum_foreground
);

} // namespace fin_ocr::chart::label::y_axis
