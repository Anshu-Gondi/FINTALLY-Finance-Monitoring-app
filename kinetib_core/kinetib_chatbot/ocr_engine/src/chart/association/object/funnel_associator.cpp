#include "fin_ocr/chart/association/object/funnel_associator.hpp"

#include "fin_ocr/chart/association/association_geometry.hpp"
#include "fin_ocr/chart/association/association_storage.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace fin_ocr::chart::association::object::funnel {

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
// FUNNEL ASSOCIATION
// =============================================================================
//
// Associates detected funnel stages with their nearest supported label.
//
// Candidate labels:
//
//     DATA_LABEL
//     UNKNOWN
//     TABLE_ROW_LABEL
//
// The association itself remains geometry-first. No value is fabricated from
// the funnel geometry.
//
// =============================================================================

void associate_funnel_objects(
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
    // COORDINATE SYSTEM
    // =========================================================================
    //
    // Funnel association does not currently use Cartesian coordinates.
    // Preserve the existing behavior and explicitly silence the parameter.
    // =========================================================================

    (void)coordinates;

    // =========================================================================
    // STAGE ASSOCIATION
    // =========================================================================

    for (
        std::size_t stage_index = 0;
        stage_index < objects.funnel_stages.size();
        ++stage_index
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

        const ::fin_ocr::chart::object::FunnelStage& stage =
            objects.funnel_stages[stage_index];

        // =====================================================================
        // STAGE CENTER
        // =====================================================================

        const auto [cx, cy] =
            ::fin_ocr::chart::association::geometry::
                rect_center(
                    stage.bounds.min_x,
                    stage.bounds.min_y,
                    stage.bounds.max_x,
                    stage.bounds.max_y
                );

        // =====================================================================
        // FIND NEAREST LABEL
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
            // Supported label classes.
            // -----------------------------------------------------------------

            if (
                label.kind !=
                    ::fin_ocr::chart::association::
                        ChartLabelKind::DATA_LABEL &&
                label.kind !=
                    ::fin_ocr::chart::association::
                        ChartLabelKind::UNKNOWN &&
                label.kind !=
                    ::fin_ocr::chart::association::
                        ChartLabelKind::TABLE_ROW_LABEL
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
                        label_index
                    );
            }
        }

        // =====================================================================
        // DISTANCE LIMIT
        // =====================================================================

        if (
            best_label < 0 ||
            best_distance >
                MAX_OBJECT_LABEL_DISTANCE
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
        // PROXIMITY SCORE
        // =====================================================================

        const double proximity =
            ::fin_ocr::chart::association::geometry::
                normalized_distance_score(
                    best_distance,
                    MAX_OBJECT_LABEL_DISTANCE
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
                    stage.confidence
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
                AssociatedObjectKind::FUNNEL_STAGE;

        association.object_index =
            static_cast<int>(
                stage_index
            );

        association.category_index =
            stage.stage_index;

        association.series_index =
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

} // namespace fin_ocr::chart::association::object::funnel
