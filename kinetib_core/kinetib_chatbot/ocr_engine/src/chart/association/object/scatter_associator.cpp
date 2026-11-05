#include "fin_ocr/chart/association/object/scatter_associator.hpp"

#include "fin_ocr/chart/association/association_geometry.hpp"
#include "fin_ocr/chart/association/association_storage.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace fin_ocr::chart::association::object::scatter {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr double MAX_OBJECT_LABEL_DISTANCE =
    config::CHART_LABEL_MAX_OBJECT_DISTANCE;

constexpr std::size_t MAX_ASSOCIATIONS =
    config::CHART_MAX_ASSOCIATIONS;

} // namespace

// =============================================================================
// SCATTER / BUBBLE ASSOCIATION
// =============================================================================
//
// Associates detected scatter points / bubbles with nearby DATA_LABEL
// instances.
//
// The point radius expands the allowable association distance so larger bubble
// markers can still match labels that are positioned farther away.
//
// No X/Y numeric value is inferred here.
// =============================================================================

void associate_scatter_objects(
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
    // CURRENTLY INDEPENDENT OF CARTESIAN COORDINATES
    // =========================================================================

    (void)coordinates;

    // =========================================================================
    // SCATTER / BUBBLE POINTS
    // =========================================================================

    for (
        std::size_t point_index = 0;
        point_index < objects.points.size();
        ++point_index
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

        const ::fin_ocr::chart::object::ScatterPoint& point =
            objects.points[point_index];

        // =====================================================================
        // FIND NEAREST DATA LABEL
        // =====================================================================

        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        for (
            std::size_t label_index = 0;
            label_index < labels.size();
            ++label_index
        ) {
            const ::fin_ocr::chart::association::ChartLabel& label =
                labels[label_index];

            // -----------------------------------------------------------------
            // Scatter / bubble association currently accepts DATA_LABEL only.
            // -----------------------------------------------------------------

            if (
                label.kind !=
                ::fin_ocr::chart::association::
                    ChartLabelKind::DATA_LABEL
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
            // Point-to-label distance.
            // -----------------------------------------------------------------

            const double distance =
                ::fin_ocr::chart::association::geometry::
                    center_distance(
                        point.x,
                        point.y,
                        label_cx,
                        label_cy
                    );

            // -----------------------------------------------------------------
            // Bubble-aware search radius.
            // -----------------------------------------------------------------

            const double allowed =
                std::max(
                    MAX_OBJECT_LABEL_DISTANCE,
                    point.radius * 4.0
                );

            if (
                distance >
                allowed
            ) {
                continue;
            }

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
        // NO MATCH
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
        // MAXIMUM DISTANCE
        // =====================================================================

        const double maximum_distance =
            std::max(
                MAX_OBJECT_LABEL_DISTANCE,
                point.radius * 4.0
            );

        // =====================================================================
        // PROXIMITY
        // =====================================================================

        const double proximity =
            ::fin_ocr::chart::association::geometry::
                normalized_distance_score(
                    best_distance,
                    maximum_distance
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
                    point.confidence
                )
            );

        // =====================================================================
        // BUILD ASSOCIATION
        // =====================================================================

        ::fin_ocr::chart::association::ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            point.is_bubble
                ? ::fin_ocr::chart::association::
                      AssociatedObjectKind::BUBBLE
                : ::fin_ocr::chart::association::
                      AssociatedObjectKind::SCATTER_POINT;

        association.object_index =
            static_cast<int>(
                point_index
            );

        association.series_index =
            point.series_index;

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
        // CENTRALIZED ASSOCIATION STORAGE
        // =====================================================================

        ::fin_ocr::chart::association::storage::
            append_association(
                associations,
                association
            );
    }
}

} // namespace fin_ocr::chart::association::object::scatter
