#pragma once

#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::interpreter::stack {

// =============================================================================
// STACKED
// =============================================================================

[[nodiscard]]
bool is_stacked(
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept;

// =============================================================================
// PERCENT STACKED
// =============================================================================

[[nodiscard]]
bool is_percent_stacked(
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept;

// =============================================================================
// CLUSTERED
// =============================================================================

[[nodiscard]]
bool is_clustered(
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept;

// =============================================================================
// COMBO
// =============================================================================

[[nodiscard]]
bool is_combo(
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept;

} // namespace fin_ocr::chart::interpreter::stack
