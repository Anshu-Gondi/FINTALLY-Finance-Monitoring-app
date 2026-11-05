#include "fin_ocr/chart/association/association_engine.hpp"

#include "fin_ocr/chart/association/dual_axis/dual_axis_associator.hpp"

#include "fin_ocr/chart/association/label/category_associator.hpp"
#include "fin_ocr/chart/association/label/label_classifier.hpp"
#include "fin_ocr/chart/association/label/series_associator.hpp"

#include "fin_ocr/chart/association/object/bar_associator.hpp"
#include "fin_ocr/chart/association/object/funnel_associator.hpp"
#include "fin_ocr/chart/association/object/path_associator.hpp"
#include "fin_ocr/chart/association/object/radial_associator.hpp"
#include "fin_ocr/chart/association/object/scatter_associator.hpp"
#include "fin_ocr/chart/association/object/treemap_associator.hpp"
#include "fin_ocr/chart/association/object/waterfall_associator.hpp"

#include "fin_ocr/chart/association/stacked/stacked_associator.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace fin_ocr::chart::association {

// =============================================================================
// COMPLETE ASSOCIATION
// =============================================================================
//
// High-level orchestration only.
//
// The engine:
//     1. creates working state
//     2. classifies labels
//     3. builds categories
//     4. builds series
//     5. establishes stack relationships
//     6. validates dual-axis state
//     7. associates object families
//     8. computes global validity
//     9. computes global confidence
//
// Individual algorithms remain in their dedicated modules.
//
// =============================================================================

ChartAssociationResult AssociationEngine::associate(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const std::vector<ChartLabel>& input_labels
) const
{
    ChartAssociationResult result{};

    // =========================================================================
    // WORKING OBJECT SET
    // =========================================================================
    //
    // Stacked association enriches BarSegment metadata.
    //
    // Keep the caller-owned object set immutable from the perspective of this
    // orchestration layer.
    //
    // =========================================================================

    ::fin_ocr::chart::object::ChartObjectSet working_objects =
        objects;

    // =========================================================================
    // WORKING LABEL SET
    // =========================================================================

    result.labels =
        input_labels;

    // =========================================================================
    // LABEL CLASSIFICATION
    // =========================================================================

    label::classify_labels(
        coordinates,
        result.labels
    );

    // =========================================================================
    // CATEGORIES
    // =========================================================================

    label::category::associate_axis_labels(
        coordinates,
        result.labels,
        result.categories
    );

    // =========================================================================
    // SERIES
    // =========================================================================

    label::series::associate_series_labels(
        coordinates,
        result.labels,
        result.series
    );

    // =========================================================================
    // STACK RELATIONSHIPS
    // =========================================================================
    //
    // Must happen before bar associations because bar associations consume
    // stack_index / series_index metadata.
    //
    // =========================================================================

    stacked::associate_stacked_objects(
        coordinates,
        working_objects
    );

    // =========================================================================
    // DUAL-AXIS ASSOCIATION
    // =========================================================================
    //
    // Current implementation is intentionally non-mutating because the object
    // ABI has no dedicated primary/secondary axis field.
    //
    // =========================================================================

    dual_axis::associate_dual_axes(
        coordinates,
        working_objects
    );

    // =========================================================================
    // BAR / COLUMN ASSOCIATION
    // =========================================================================

    object::bar::associate_bars(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    // =========================================================================
    // LINE / PATH ASSOCIATION
    // =========================================================================

    object::path::associate_paths(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    // =========================================================================
    // RADIAL ASSOCIATION
    // =========================================================================

    object::radial::associate_radial_objects(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    // =========================================================================
    // SCATTER / BUBBLE ASSOCIATION
    // =========================================================================

    object::scatter::associate_scatter_objects(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    // =========================================================================
    // WATERFALL ASSOCIATION
    // =========================================================================

    object::waterfall::associate_waterfall_objects(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    // =========================================================================
    // FUNNEL ASSOCIATION
    // =========================================================================

    object::funnel::associate_funnel_objects(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    // =========================================================================
    // TREEMAP ASSOCIATION
    // =========================================================================

    object::treemap::associate_treemap_objects(
        coordinates,
        working_objects,
        result.labels,
        result.associations
    );

    // =========================================================================
    // GLOBAL VALIDITY
    // =========================================================================

    const bool has_labels =
        !result.labels.empty();

    const bool has_objects =
        objects.valid ||
        !objects.bars.empty() ||
        !objects.paths.empty() ||
        !objects.slices.empty() ||
        !objects.points.empty() ||
        !objects.waterfall_steps.empty() ||
        !objects.funnel_stages.empty() ||
        !objects.treemap_nodes.empty();

    const bool has_relationships =
        !result.categories.empty() ||
        !result.series.empty() ||
        !result.associations.empty();

    result.valid =
        has_labels &&
        has_objects &&
        has_relationships;

    // =========================================================================
    // GLOBAL CONFIDENCE
    // =========================================================================

    double confidence_sum =
        0.0;

    std::size_t confidence_count =
        0;

    // -------------------------------------------------------------------------
    // Object-set confidence.
    // -------------------------------------------------------------------------

    if (
        objects.confidence > 0.0f
    ) {
        confidence_sum +=
            static_cast<double>(
                objects.confidence
            );

        ++confidence_count;
    }

    // -------------------------------------------------------------------------
    // Label confidence.
    // -------------------------------------------------------------------------

    for (
        const ChartLabel& label :
        result.labels
    ) {
        if (
            label.confidence <= 0.0f
        ) {
            continue;
        }

        confidence_sum +=
            static_cast<double>(
                label.confidence
            );

        ++confidence_count;
    }

    // -------------------------------------------------------------------------
    // Category confidence.
    // -------------------------------------------------------------------------

    for (
        const ChartCategory& category :
        result.categories
    ) {
        confidence_sum +=
            static_cast<double>(
                category.confidence
            );

        ++confidence_count;
    }

    // -------------------------------------------------------------------------
    // Series confidence.
    // -------------------------------------------------------------------------

    for (
        const ChartSeries& series :
        result.series
    ) {
        confidence_sum +=
            static_cast<double>(
                series.confidence
            );

        ++confidence_count;
    }

    // -------------------------------------------------------------------------
    // Association confidence.
    // -------------------------------------------------------------------------

    for (
        const ChartAssociation& association :
        result.associations
    ) {
        confidence_sum +=
            static_cast<double>(
                association.confidence
            );

        ++confidence_count;
    }

    // =========================================================================
    // FINAL CONFIDENCE
    // =========================================================================

    if (
        confidence_count == 0
    ) {
        result.confidence =
            0.0f;

    } else {
        result.confidence =
            static_cast<float>(
                std::clamp(
                    confidence_sum /
                        static_cast<double>(
                            confidence_count
                        ),
                    0.0,
                    1.0
                )
            );
    }

    return result;
}

} // namespace fin_ocr::chart::association
