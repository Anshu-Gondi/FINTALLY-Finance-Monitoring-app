#pragma once

#include <vector>

#include "fin_ocr/chart/label/label_types.hpp"

namespace fin_ocr::chart::label::merger {

// =============================================================================
// DUPLICATE LABEL
// =============================================================================

[[nodiscard]]
bool duplicate_label(
    const DetectedLabel& candidate,
    const std::vector<DetectedLabel>& existing
) noexcept;

} // namespace fin_ocr::chart::label::merger
