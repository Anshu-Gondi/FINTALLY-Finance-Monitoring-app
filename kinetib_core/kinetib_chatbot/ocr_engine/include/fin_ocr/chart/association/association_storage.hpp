#pragma once

#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"

namespace fin_ocr::chart::association::storage {

// =============================================================================
// ASSOCIATION EXISTENCE
// =============================================================================

[[nodiscard]]
bool association_exists(
    const std::vector<ChartAssociation>& associations,
    int label_index,
    AssociatedObjectKind object_kind,
    int object_index
) noexcept;

// =============================================================================
// ASSOCIATION INSERTION
// =============================================================================
//
// Applies shared association policies:
//
//     - maximum association count
//     - valid indexes
//     - minimum confidence
//     - duplicate suppression
//
// =============================================================================

void append_association(
    std::vector<ChartAssociation>& associations,
    const ChartAssociation& association
);

} // namespace fin_ocr::chart::association::storage
