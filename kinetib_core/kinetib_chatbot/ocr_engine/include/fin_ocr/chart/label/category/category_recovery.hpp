#pragma once

#include <cstdint>
#include <vector>

#include "fin_ocr/chart/label/label_types.hpp"
#include "fin_ocr/chart/label/category/category_sequence.hpp"

namespace fin_ocr::chart::label::category {

// =============================================================================
// MISSING CATEGORY RECOVERY
// =============================================================================

[[nodiscard]]
std::vector<TextCandidate> recover_missing_category_candidates(
    const std::uint8_t* chart_buffer,
    int image_width,
    int image_height,
    const std::vector<TextCandidate>& category_candidates,
    const CategorySequenceStats& sequence_stats,
    std::uint8_t minimum_foreground
);

} // namespace fin_ocr::chart::label::category
