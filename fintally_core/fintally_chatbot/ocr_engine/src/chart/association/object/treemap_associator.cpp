#include "fin_ocr/chart/association/object/treemap_associator.hpp"

#include "fin_ocr/chart/association/association_geometry.hpp"
#include "fin_ocr/chart/association/association_storage.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace fin_ocr::chart::association::object::treemap {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr std::size_t MAX_ASSOCIATIONS =
    config::CHART_MAX_ASSOCIATIONS;

} // namespace

// =============================================================================
// TREEMAP ASSOCIATION
// =============================================================================
//
// Associates already-detected treemap nodes with labels physically contained
// by the node.
//
// Supported labels:
//
//     DATA_LABEL
//     UNKNOWN
//
// The node hierarchy has already been established by the object detector.
// This module only creates the semantic label -> node association.
//
// =============================================================================

void associate_treemap_objects(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const std::vector<
        ::fin_ocr::chart::association::ChartLabel
    >& labels,
    std::vector<
        ::fin_ocr::chart::association::ChartAssociation
    >& associations
)
{
    // =========================================================================
    // CURRENT IMPLEMENTATION DOES NOT USE CARTESIAN COORDINATES
    // =========================================================================

    (void)coordinates;

    // =========================================================================
    // TREEMAP NODES
    // =========================================================================

    for (
        std::size_t node_index = 0;
        node_index < objects.treemap_nodes.size();
        ++node_index
    ) {
        // ---------------------------------------------------------------------
        // Association capacity.
        // ---------------------------------------------------------------------

        if (
            associations.size() >=
            MAX_ASSOCIATIONS
        ) {
            return;
        }

        const ::fin_ocr::chart::object::TreemapNode& node =
            objects.treemap_nodes[node_index];

        // =====================================================================
        // FIND LABEL INSIDE NODE
        // =====================================================================
        //
        // Prefer a label physically contained within the detected node.
        //
        // Among multiple valid labels, choose the one whose center is closest
        // to the node center.
        // =====================================================================

        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        const auto [node_cx, node_cy] =
            ::fin_ocr::chart::association::geometry::
                rect_center(
                    node.bounds.min_x,
                    node.bounds.min_y,
                    node.bounds.max_x,
                    node.bounds.max_y
                );

        for (
            std::size_t label_index = 0;
            label_index < labels.size();
            ++label_index
        ) {
            const ::fin_ocr::chart::association::ChartLabel& label =
                labels[label_index];

            // -----------------------------------------------------------------
            // Supported label classes.
            // -----------------------------------------------------------------

            if (
                label.kind !=
                    ::fin_ocr::chart::association::
                        ChartLabelKind::DATA_LABEL &&
                label.kind !=
                    ::fin_ocr::chart::association::
                        ChartLabelKind::UNKNOWN
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // Label center.
            // -----------------------------------------------------------------

            const auto [label_cx, label_cy] =
                ::fin_ocr::chart::association::geometry::
                    rect_center(
                        label.min_x,
                        label.min_y,
                        label.max_x,
                        label.max_y
                    );

            // -----------------------------------------------------------------
            // Require the complete label rectangle to fit inside the node.
            // -----------------------------------------------------------------

            const bool inside =
                label.min_x >=
                    node.bounds.min_x &&
                label.max_x <=
                    node.bounds.max_x &&
                label.min_y >=
                    node.bounds.min_y &&
                label.max_y <=
                    node.bounds.max_y;

            if (
                !inside
            ) {
                continue;
            }

            // -----------------------------------------------------------------
            // Center distance.
            // -----------------------------------------------------------------

            const double distance =
                ::fin_ocr::chart::association::geometry::
                    center_distance(
                        node_cx,
                        node_cy,
                        label_cx,
                        label_cy
                    );

            if (
                distance <
                best_distance
            ) {
                best_distance =
                    distance;

                best_label =
                    static_cast<int>(
                        label_index
                    );
            }
        }

        // =====================================================================
        // NO VALID LABEL
        // =====================================================================

        if (
            best_label < 0
        ) {
            continue;
        }

        const ::fin_ocr::chart::association::ChartLabel& label =
            labels[
                static_cast<std::size_t>(
                    best_label
                )
            ];

        // =====================================================================
        // NORMALIZATION RANGE
        // =====================================================================
        //
        // The current implementation scales distance against the approximate
        // node perimeter rather than an externally configured object-label
        // distance.
        // =====================================================================

        const double max_distance =
            std::max(
                1.0,
                static_cast<double>(
                    node.bounds.max_x -
                    node.bounds.min_x +
                    node.bounds.max_y -
                    node.bounds.min_y
                )
            );

        // =====================================================================
        // PROXIMITY
        // =====================================================================

        const double proximity =
            ::fin_ocr::chart::association::geometry::
                normalized_distance_score(
                    best_distance,
                    max_distance
                );

        // =====================================================================
        // CONFIDENCE
        // =====================================================================

        const double confidence =
            proximity *
            std::max(
                0.0,
                static_cast<double>(
                    label.confidence
                )
            ) *
            std::max(
                0.20,
                static_cast<double>(
                    node.confidence
                )
            );

        // =====================================================================
        // BUILD ASSOCIATION
        // =====================================================================

        ::fin_ocr::chart::association::ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            ::fin_ocr::chart::association::
                AssociatedObjectKind::TREEMAP_RECTANGLE;

        association.object_index =
            static_cast<int>(
                node_index
            );

        association.category_index =
            -1;

        association.series_index =
            -1;

        association.stack_index =
            node.hierarchy_level;

        association.distance =
            best_distance;

        association.confidence =
            static_cast<float>(
                std::clamp(
                    confidence,
                    0.0,
                    1.0
                )
            );

        // =====================================================================
        // CENTRALIZED ASSOCIATION STORAGE
        // =====================================================================

        ::fin_ocr::chart::association::storage::
            append_association(
                associations,
                association
            );
    }
}

} // namespace fin_ocr::chart::association::object::treemap
