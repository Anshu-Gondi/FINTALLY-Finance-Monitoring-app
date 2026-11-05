#pragma once

#include "fin_ocr/chart/label/label_types.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_types.hpp"

namespace fin_ocr::chart::label::y_axis {

// =============================================================================
// GEOMETRY
// =============================================================================

[[nodiscard]]
bool acceptable_y_axis_geometry(
    const TextCandidate& candidate,
    int image_width,
    int image_height,
    YAxisKind axis
) noexcept;

// =============================================================================
// X POSITION
// =============================================================================

[[nodiscard]]
double y_axis_x_score(
    const TextCandidate& candidate,
    int image_width,
    YAxisKind axis
) noexcept;

// =============================================================================
// HEIGHT
// =============================================================================

[[nodiscard]]
double y_axis_height_score(
    const TextCandidate& candidate,
    int image_height
) noexcept;

// =============================================================================
// VERTICAL POSITION
// =============================================================================

[[nodiscard]]
double y_axis_vertical_position_score(
    const TextCandidate& candidate,
    int image_height
) noexcept;

// =============================================================================
// COMPLETE SCORE
// =============================================================================

[[nodiscard]]
double y_axis_candidate_score(
    const TextCandidate& candidate,
    int image_width,
    int image_height,
    YAxisKind axis
) noexcept;

} // namespace fin_ocr::chart::label::y_axis
