#pragma once

#include <string>
#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"

namespace fin_ocr::chart::label::output {

// =============================================================================
// INVALID INPUT
// =============================================================================

[[nodiscard]]
std::string format_invalid_chart_buffer();

// =============================================================================
// NO LABELS
// =============================================================================

[[nodiscard]]
std::string format_empty_labels();

// =============================================================================
// FORMATTED LABEL OUTPUT
// =============================================================================

[[nodiscard]]
std::string format_labels(
    const std::vector<
        ::fin_ocr::chart::association::ChartLabel
    >& labels
);

} // namespace fin_ocr::chart::label::output
