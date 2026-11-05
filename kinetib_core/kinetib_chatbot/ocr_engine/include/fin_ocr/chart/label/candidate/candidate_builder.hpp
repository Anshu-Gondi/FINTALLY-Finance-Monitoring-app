#pragma once

#include <cstdint>
#include <vector>

#include "fin_ocr/chart/label/label_types.hpp"
#include "fin_ocr/chart/label/candidate/candidate_segmenter.hpp"

namespace fin_ocr::chart::label::candidate {

// =============================================================================
// COMPLETE BAND CANDIDATE BUILD
// =============================================================================

[[nodiscard]]
std::vector<TextCandidate> build_band_candidates(
    const std::uint8_t* chart_buffer,
    int width,
    int y0,
    int y1,
    std::uint8_t minimum_foreground,
    int minimum_component_width
);

} // namespace fin_ocr::chart::label::candidate
