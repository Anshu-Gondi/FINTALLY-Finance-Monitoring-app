#pragma once

#include <string>

#include "fin_ocr/chart/label/label_types.hpp"

namespace fin_ocr::chart::label::filter {

// =============================================================================
// REJECTION REASON
// =============================================================================

[[nodiscard]]
const char* candidate_reject_reason_string(
    CandidateRejectReason reason
) noexcept;

// =============================================================================
// TEXT QUALITY
// =============================================================================

[[nodiscard]]
bool acceptable_label_text(
    const std::string& text,
    std::size_t glyph_count
) noexcept;

[[nodiscard]]
bool reject_geometry_like_text(
    const std::string& text
) noexcept;

// =============================================================================
// CANDIDATE GEOMETRY
// =============================================================================

[[nodiscard]]
bool acceptable_candidate_geometry(
    const TextCandidate& candidate,
    int image_width,
    int image_height,
    int max_band_height
) noexcept;

// =============================================================================
// LABEL CONFIDENCE
// =============================================================================

[[nodiscard]]
float label_confidence(
    float density,
    int width,
    int height,
    std::size_t glyph_count
) noexcept;

} // namespace fin_ocr::chart::label::filter
