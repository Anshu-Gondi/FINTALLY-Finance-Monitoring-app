#pragma once

#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::association {

// =============================================================================
// ASSOCIATION ENGINE
// =============================================================================
//
// High-level orchestration layer for chart semantic association.
//
// Individual stages are implemented by dedicated modules:
//
//     label classification
//     category association
//     series association
//     object association
//     stack association
//     dual-axis association
//
// This class owns orchestration only.
//
// =============================================================================

class AssociationEngine {
public:

    AssociationEngine() = default;

    ~AssociationEngine() = default;

    AssociationEngine(
        const AssociationEngine&
    ) = default;

    AssociationEngine& operator=(
        const AssociationEngine&
    ) = default;

    AssociationEngine(
        AssociationEngine&&
    ) noexcept = default;

    AssociationEngine& operator=(
        AssociationEngine&&
    ) noexcept = default;

    // =========================================================================
    // ASSOCIATE
    // =========================================================================

    [[nodiscard]]
    ChartAssociationResult associate(
        const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
        const ::fin_ocr::chart::object::ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels
    ) const;
};

} // namespace fin_ocr::chart::association
