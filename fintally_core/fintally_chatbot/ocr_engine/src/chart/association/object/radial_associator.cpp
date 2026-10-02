#include "fin_ocr/chart/association/object/radial_associator.hpp"

#include "fin_ocr/chart/association/association_geometry.hpp"
#include "fin_ocr/chart/association/association_storage.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace fin_ocr::chart::association::object::radial {

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
// RADIAL OBJECT ASSOCIATION
// =============================================================================
//
// Associates PIE / DONUT slices with nearby semantic labels.
//
// Candidate labels:
//
//     DATA_LABEL
//     LEGEND_LABEL
//     SERIES_LABEL
//
// The slice geometry is already produced by the radial object detector.
// This module only establishes the label -> slice relationship.
//
// =============================================================================

void associate_radial_objects(
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
    // CURRENTLY NOT A CARTESIAN OPERATION
    // =========================================================================

    (void)coordinates;

    // =========================================================================
    // RADIAL SLICES
    // =========================================================================

    for (
        std::size_t slice_index = 0;
        slice_index < objects.slices.size();
        ++slice_index
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

        const ::fin_ocr::chart::object::RadialSlice& slice =
            objects.slices[slice_index];

        // =====================================================================
        // SLICE MID-ANGLE
        // =====================================================================

        const double mid_angle =
            (
                slice.start_angle +
                slice.end_angle
            ) *
            0.5;

        // =====================================================================
        // LABEL ANCHOR RADIUS
        // =====================================================================
        //
        // Donut:
        //     use the midpoint between inner and outer radius.
        //
        // Pie:
        //     use 70% of the outer radius.
        //
        // This preserves the existing geometric association rule.
        // =====================================================================

        const double radius =
            slice.inner_radius > 0
                ? (
                    static_cast<double>(
                        slice.inner_radius
                    ) +
                    static_cast<double>(
                        slice.outer_radius
                    )
                ) *
                    0.5
                : static_cast<double>(
                      slice.outer_radius
                  ) *
                    0.70;

        // =====================================================================
        // SLICE ASSOCIATION POINT
        // =====================================================================

        const int object_x =
            static_cast<int>(
                std::lround(
                    static_cast<double>(
                        slice.center_x
                    ) +
                    std::cos(
                        mid_angle
                    ) *
                        radius
                )
            );

        const int object_y =
            static_cast<int>(
                std::lround(
                    static_cast<double>(
                        slice.center_y
                    ) +
                    std::sin(
                        mid_angle
                    ) *
                        radius
                )
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
                        ChartLabelKind::DATA_LABEL &&
                label.kind !=
                    ::fin_ocr::chart::association::
                        ChartLabelKind::LEGEND_LABEL &&
                label.kind !=
                    ::fin_ocr::chart::association::
                        ChartLabelKind::SERIES_LABEL
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
            // Distance from slice anchor to label center.
            // -----------------------------------------------------------------

            const double distance =
                ::fin_ocr::chart::association::geometry::
                    center_distance(
                        object_x,
                        object_y,
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
        // ASSOCIATION CONFIDENCE
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
                    slice.confidence
                )
            );

        // =====================================================================
        // BUILD ASSOCIATION
        // =====================================================================

        ::fin_ocr::chart::association::ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            slice.inner_radius > 0
                ? ::fin_ocr::chart::association::
                      AssociatedObjectKind::DONUT_SLICE
                : ::fin_ocr::chart::association::
                      AssociatedObjectKind::PIE_SLICE;

        association.object_index =
            static_cast<int>(
                slice_index
            );

        association.series_index =
            -1;

        association.category_index =
            slice.category_index;

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

} // namespace fin_ocr::chart::association::object::radial
