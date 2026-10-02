#pragma once

#include <string>

namespace fin_ocr::chart::label::category {

// =============================================================================
// MONTH CANONICALIZATION
// =============================================================================

[[nodiscard]]
const char* canonical_month_label(
    const std::string& text
) noexcept;

// =============================================================================
// CATEGORY LABEL CANONICALIZATION
// =============================================================================

[[nodiscard]]
bool canonicalize_category_label(
    std::string& text
) noexcept;

} // namespace fin_ocr::chart::label::category
