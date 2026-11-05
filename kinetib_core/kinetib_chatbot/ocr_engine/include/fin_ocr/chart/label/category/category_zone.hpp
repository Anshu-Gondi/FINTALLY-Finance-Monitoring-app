#pragma once

#include "fin_ocr/chart/label/label_types.hpp"

namespace fin_ocr::chart::label::category {

// =============================================================================
// CATEGORY ZONE
// =============================================================================

[[nodiscard]]
LabelZone classify_label_zone(
    const TextCandidate& candidate,
    int image_width,
    int image_height
) noexcept;

// =============================================================================
// CATEGORY GEOMETRY
// =============================================================================

[[nodiscard]]
bool acceptable_category_geometry(
    const TextCandidate& candidate,
    int image_width,
    int image_height
) noexcept;

} // namespace fin_ocr::chart::label::category
