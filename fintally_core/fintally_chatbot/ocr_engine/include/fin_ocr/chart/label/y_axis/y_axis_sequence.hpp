#pragma once

#include <vector>

#include "fin_ocr/chart/label/label_types.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_types.hpp"

namespace fin_ocr::chart::label::y_axis {

// =============================================================================
// SPACING
// =============================================================================

[[nodiscard]]
double y_axis_median_spacing(
    const std::vector<TextCandidate>& candidates
) noexcept;

// =============================================================================
// STATISTICS
// =============================================================================

[[nodiscard]]
YAxisSequenceStats build_y_axis_sequence_stats(
    const std::vector<TextCandidate>& candidates
) noexcept;

// =============================================================================
// X SCORE
// =============================================================================

[[nodiscard]]
double y_axis_sequence_x_score(
    const TextCandidate& candidate,
    const YAxisSequenceStats& stats
) noexcept;

// =============================================================================
// HEIGHT SCORE
// =============================================================================

[[nodiscard]]
double y_axis_sequence_height_score(
    const TextCandidate& candidate,
    const YAxisSequenceStats& stats
) noexcept;

// =============================================================================
// SPACING SCORE
// =============================================================================

[[nodiscard]]
double y_axis_sequence_spacing_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    const YAxisSequenceStats& stats
) noexcept;

// =============================================================================
// COMPLETE SCORE
// =============================================================================

[[nodiscard]]
double y_axis_sequence_score(
    const TextCandidate& candidate,
    const std::vector<TextCandidate>& candidates,
    const YAxisSequenceStats& stats
) noexcept;

} // namespace fin_ocr::chart::label::y_axis
