#pragma once

#include <string>

#include "fin_ocr/chart/association/association_types.hpp"

namespace fin_ocr::chart::interpreter::lookup {

// =============================================================================
// LABEL
// =============================================================================

[[nodiscard]]
const ::fin_ocr::chart::association::ChartLabel*
find_label(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    int label_index
) noexcept;

// =============================================================================
// CATEGORY
// =============================================================================

[[nodiscard]]
const ::fin_ocr::chart::association::ChartCategory*
find_category(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    int category_index
) noexcept;

// =============================================================================
// SERIES
// =============================================================================

[[nodiscard]]
const ::fin_ocr::chart::association::ChartSeries*
find_series(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    int series_index
) noexcept;

// =============================================================================
// OBJECT ASSOCIATION
// =============================================================================

[[nodiscard]]
const ::fin_ocr::chart::association::ChartAssociation*
find_object_association(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    ::fin_ocr::chart::association::AssociatedObjectKind object_kind,
    int object_index
) noexcept;

// =============================================================================
// CATEGORY NAME
// =============================================================================

[[nodiscard]]
std::string category_name(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    int category_index
);

// =============================================================================
// SERIES NAME
// =============================================================================

[[nodiscard]]
std::string series_name(
    const ::fin_ocr::chart::association::ChartAssociationResult& associations,
    int series_index
);

} // namespace fin_ocr::chart::interpreter::lookup
