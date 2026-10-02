#pragma once

#include <cstdint>

#include "fin_ocr/chart/label/label_types.hpp"

namespace fin_ocr::chart::label::geometry {

// =============================================================================
// CANDIDATE VALIDATION
// =============================================================================

[[nodiscard]]
bool valid_candidate(
    const TextCandidate& candidate
) noexcept;

// =============================================================================
// OVERLAP
// =============================================================================

[[nodiscard]]
bool vertical_overlap(
    int a_min_y,
    int a_max_y,
    int b_min_y,
    int b_max_y
) noexcept;

[[nodiscard]]
bool horizontal_overlap(
    int a_min_x,
    int a_max_x,
    int b_min_x,
    int b_max_x
) noexcept;

// =============================================================================
// CHART FOREGROUND
// =============================================================================
//
// Reads channel 1 from the 3-channel chart representation.
//
// =============================================================================

[[nodiscard]]
std::uint8_t chart_foreground(
    const std::uint8_t* chart_buffer,
    int width,
    int x,
    int y
) noexcept;

} // namespace fin_ocr::chart::label::geometry
