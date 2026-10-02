#pragma once

#include <vector>

#include "fin_ocr/chart/label/label_types.hpp"

namespace fin_ocr::chart::label::category {

// =============================================================================
// CENTER
// =============================================================================

[[nodiscard]]
double category_center_score(
    const TextCandidate& candidate,
    int image_height
) noexcept;

// =============================================================================
// HEIGHT
// =============================================================================

[[nodiscard]]
double category_height_score(
    const TextCandidate& candidate,
    int image_height
) noexcept;

// =============================================================================
// WIDTH
// =============================================================================

[[nodiscard]]
double category_width_score(
    const TextCandidate& candidate,
    int image_width
) noexcept;

// =============================================================================
// DENSITY
// =============================================================================

[[nodiscard]]
double category_density_score(
    const TextCandidate& candidate
) noexcept;

// =============================================================================
// ALIGNMENT
// =============================================================================

[[nodiscard]]
double category_alignment_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    int image_width,
    int image_height
) noexcept;

// =============================================================================
// COMPLETE CATEGORY SCORE
// =============================================================================

[[nodiscard]]
double category_candidate_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    int image_width,
    int image_height
) noexcept;

} // namespace fin_ocr::chart::label::category
