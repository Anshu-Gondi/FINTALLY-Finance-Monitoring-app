#pragma once

#include <vector>

#include "fin_ocr/chart/label/label_types.hpp"

namespace fin_ocr::chart::label::category {

// =============================================================================
// CATEGORY SEQUENCE MODEL
// =============================================================================

struct CategorySequenceStats {

    double median_center_y = 0.0;

    double median_height = 0.0;

    double median_width = 0.0;

    double median_center_spacing = 0.0;

    std::size_t candidate_count = 0;

    std::size_t coherent_count = 0;
};

// =============================================================================
// BUILD STATISTICS
// =============================================================================

[[nodiscard]]
CategorySequenceStats build_category_sequence_stats(
    const std::vector<TextCandidate>& category_candidates
) noexcept;

// =============================================================================
// BASELINE SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_baseline_score(
    const TextCandidate& candidate,
    const CategorySequenceStats& stats
) noexcept;

// =============================================================================
// HEIGHT SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_height_score(
    const TextCandidate& candidate,
    const CategorySequenceStats& stats
) noexcept;

// =============================================================================
// SPACING SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_spacing_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& category_candidates,
    const CategorySequenceStats& stats
) noexcept;

// =============================================================================
// COMPLETE SEQUENCE SCORE
// =============================================================================

[[nodiscard]]
double category_sequence_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& category_candidates,
    const CategorySequenceStats& stats
) noexcept;

} // namespace fin_ocr::chart::label::category
