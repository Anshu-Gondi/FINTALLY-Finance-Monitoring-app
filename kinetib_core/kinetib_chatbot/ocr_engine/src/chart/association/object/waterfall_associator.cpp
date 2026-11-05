#include "fin_ocr/chart/association/object/waterfall_associator.hpp"

#include "fin_ocr/chart/association/association_geometry.hpp"
#include "fin_ocr/chart/association/association_storage.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace fin_ocr::chart::association::object::waterfall {

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
// WATERFALL ASSOCIATION
// =============================================================================
//
// Associates detected waterfall steps with their nearest supported label.
//
// Supported labels:
//
//     X_AXIS_LABEL
//     DATA_LABEL
//
// This module performs semantic association only.
// It does not infer waterfall values.
//
// =============================================================================

void associate_waterfall_objects(
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
    // WATERFALL STEPS
    // =========================================================================

    for (
        std::size_t step_index = 0;
        step_index < objects.waterfall_steps.size();
        ++step_index
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

        const ::fin_ocr::chart::object::WaterfallStep& step =
            objects.waterfall_steps[step_index];

        // =====================================================================
        // STEP CENTER
        // =====================================================================

        const auto [cx, cy] =
            ::fin_ocr::chart::association::geometry::
                rect_center(
                    step.bounds.min_x,
                    step.bounds.min_y,
                    step.bounds.max_x,
                    step.bounds.max_y
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
            // Supported label types.
            // -----------------------------------------------------------------

            if (
                label.kind !=
                    ::fin_ocr::chart::association::
                        ChartLabelKind::X_AXIS_LABEL &&
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
            // Center distance.
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
        // PROXIMITY
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
                    step.confidence
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
                AssociatedObjectKind::WATERFALL_STEP;

        association.object_index =
            static_cast<int>(
                step_index
            );

        association.category_index =
            step.category_index;

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
        // CENTRALIZED STORAGE
        // =====================================================================

        ::fin_ocr::chart::association::storage::
            append_association(
                associations,
                association
            );
    }
}

} // namespace fin_ocr::chart::association::object::waterfall
