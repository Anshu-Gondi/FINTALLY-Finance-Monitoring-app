#pragma once

#include <string>

#include "fin_ocr/chart/label/y_axis/y_axis_types.hpp"

namespace fin_ocr::chart::label::y_axis {

// =============================================================================
// TEXT COMPACTION
// =============================================================================

void compact_axis_text(
    std::string& text
);

// =============================================================================
// Y-AXIS TEXT NORMALIZATION
// =============================================================================

[[nodiscard]]
bool normalize_y_axis_text(
    std::string& text,
    YAxisKind axis
) noexcept;

// =============================================================================
// NUMERIC PARSING
// =============================================================================

[[nodiscard]]
bool parse_y_axis_numeric_value(
    const std::string& text,
    YAxisKind axis,
    double& value
) noexcept;

} // namespace fin_ocr::chart::label::y_axis
