#include "fin_ocr/chart/association/object/path_associator.hpp"

#include "fin_ocr/chart/association/association_geometry.hpp"
#include "fin_ocr/chart/association/association_storage.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace fin_ocr::chart::association::object::path {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr double MAX_SERIES_LABEL_DISTANCE =
    config::CHART_LABEL_MAX_SERIES_DISTANCE;

constexpr std::size_t MAX_ASSOCIATIONS =
    config::CHART_MAX_ASSOCIATIONS;

} // namespace

// =============================================================================
// PATH / AREA ASSOCIATION
// =============================================================================
//
// Associates detected chart paths with the nearest series / legend label.
//
// Current behavior:
//
//     SERIES_LABEL -> path
//     LEGEND_LABEL -> path
//
// The path detector has already produced the geometric path. This module only
// establishes the semantic label relationship.
//
// =============================================================================

void associate_paths(
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
    // CURRENT IMPLEMENTATION DOES NOT USE COORDINATES
    // =========================================================================

    (void)coordinates;

    // =========================================================================
    // PATHS
    // =========================================================================

    for (
        std::size_t path_index = 0;
        path_index < objects.paths.size();
        ++path_index
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

        const ::fin_ocr::chart::object::ChartPath& path =
            objects.paths[path_index];

        // ---------------------------------------------------------------------
        // Derive path bounding rectangle from its points.
        // ---------------------------------------------------------------------

        const ::fin_ocr::chart::object::ChartRect bounds =
            ::fin_ocr::chart::association::geometry::
                path_bounds(
                    path
                );

        // ---------------------------------------------------------------------
        // Reject empty or invalid paths.
        // ---------------------------------------------------------------------

        if (
            path.points.empty() ||
            !::fin_ocr::chart::association::geometry::
                valid_rect(
                    bounds.min_x,
                    bounds.min_y,
                    bounds.max_x,
                    bounds.max_y
                )
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Path center.
        // ---------------------------------------------------------------------

        const auto [cx, cy] =
            ::fin_ocr::chart::association::geometry::
                object_center(
                    bounds
                );

        // =====================================================================
        // SEARCH FOR NEAREST SERIES / LEGEND LABEL
        // =====================================================================

        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        for (
            std::size_t i = 0;
            i < labels.size();
            ++i
        ) {
            const ::fin_ocr::chart::association::ChartLabel& label =
                labels[i];

            if (
                label.kind !=
                    ::fin_ocr::chart::association::
                        ChartLabelKind::SERIES_LABEL &&
                label.kind !=
                    ::fin_ocr::chart::association::
                        ChartLabelKind::LEGEND_LABEL
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
            // Euclidean center distance.
            // -----------------------------------------------------------------

            const double distance =
                ::fin_ocr::chart::association::geometry::
                    center_distance(
                        cx,
                        cy,
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
                        i
                    );
            }
        }

        // =====================================================================
        // DISTANCE VALIDATION
        // =====================================================================

        if (
            best_label < 0 ||
            best_distance >
                MAX_SERIES_LABEL_DISTANCE
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
        // PROXIMITY
        // =====================================================================

        const double proximity =
            ::fin_ocr::chart::association::geometry::
                normalized_distance_score(
                    best_distance,
                    MAX_SERIES_LABEL_DISTANCE
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
                    path.confidence
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
                AssociatedObjectKind::LINE_SEGMENT;

        association.object_index =
            static_cast<int>(
                path_index
            );

        association.series_index =
            label.series_index >= 0
                ? label.series_index
                : path.series_index;

        association.category_index =
            -1;

        association.stack_index =
            -1;

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
        // CENTRALIZED INSERTION
        // =====================================================================

        ::fin_ocr::chart::association::storage::
            append_association(
                associations,
                association
            );
    }
}

} // namespace fin_ocr::chart::association::object::path
