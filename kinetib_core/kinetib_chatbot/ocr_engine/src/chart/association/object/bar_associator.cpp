#include "fin_ocr/chart/association/object/bar_associator.hpp"

#include "fin_ocr/chart/association/association_geometry.hpp"
#include "fin_ocr/chart/association/association_storage.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace fin_ocr::chart::association::object::bar {

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
// BAR / COLUMN ASSOCIATION
// =============================================================================
//
// Associates detected bars/columns with their nearest relevant axis label.
//
// Column:
//     object -> X-axis category label
//
// Bar:
//     object -> Y-axis label
//
// Shared association insertion policy is delegated to association::storage.
//
// =============================================================================

void associate_bars(
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
    for (
        std::size_t bar_index = 0;
        bar_index < objects.bars.size();
        ++bar_index
    ) {
        // =========================================================================
        // ASSOCIATION CAPACITY
        // =========================================================================

        if (
            associations.size() >=
            MAX_ASSOCIATIONS
        ) {
            return;
        }

        const ::fin_ocr::chart::object::BarSegment& bar =
            objects.bars[bar_index];

        const ::fin_ocr::chart::object::ChartRect& bounds =
            bar.bounds;

        // =========================================================================
        // OBJECT ORIENTATION
        // =========================================================================

        const int width =
            bounds.max_x -
            bounds.min_x +
            1;

        const int height =
            bounds.max_y -
            bounds.min_y +
            1;

        const bool vertical =
            height >
            width;

        int best_label =
            -1;

        double best_distance =
            std::numeric_limits<double>::infinity();

        // =========================================================================
        // COLUMN -> X-AXIS CATEGORY
        // =========================================================================

        if (
            vertical &&
            coordinates.x_axis.horizontal
        ) {
            const int object_center_x =
                (
                    bounds.min_x +
                    bounds.max_x
                ) / 2;

            for (
                std::size_t i = 0;
                i < labels.size();
                ++i
            ) {
                const ::fin_ocr::chart::association::ChartLabel& label =
                    labels[i];

                if (
                    label.kind !=
                    ::fin_ocr::chart::association::ChartLabelKind::
                        X_AXIS_LABEL
                ) {
                    continue;
                }

                const auto [label_cx, label_cy] =
                    ::fin_ocr::chart::association::geometry::
                        rect_center(
                            label.min_x,
                            label.min_y,
                            label.max_x,
                            label.max_y
                        );

                const double horizontal_delta =
                    std::abs(
                        static_cast<double>(
                            object_center_x -
                            label_cx
                        )
                    );

                const double vertical_delta =
                    std::abs(
                        static_cast<double>(
                            bounds.max_y -
                            label_cy
                        )
                    );

                const double distance =
                    horizontal_delta +
                    vertical_delta *
                        0.35;

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

        // =========================================================================
        // BAR -> Y-AXIS CATEGORY
        // =========================================================================

        } else if (
            coordinates.y_axis.vertical
        ) {
            const int object_center_y =
                (
                    bounds.min_y +
                    bounds.max_y
                ) / 2;

            for (
                std::size_t i = 0;
                i < labels.size();
                ++i
            ) {
                const ::fin_ocr::chart::association::ChartLabel& label =
                    labels[i];

                if (
                    label.kind !=
                    ::fin_ocr::chart::association::ChartLabelKind::
                        Y_AXIS_LABEL
                ) {
                    continue;
                }

                const auto [label_cx, label_cy] =
                    ::fin_ocr::chart::association::geometry::
                        rect_center(
                            label.min_x,
                            label.min_y,
                            label.max_x,
                            label.max_y
                        );

                const double vertical_delta =
                    std::abs(
                        static_cast<double>(
                            object_center_y -
                            label_cy
                        )
                    );

                const double horizontal_delta =
                    std::abs(
                        static_cast<double>(
                            bounds.min_x -
                            label_cx
                        )
                    );

                const double distance =
                    vertical_delta +
                    horizontal_delta *
                        0.35;

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
        }

        // =========================================================================
        // NO VALID LABEL
        // =========================================================================

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

        // =========================================================================
        // PROXIMITY
        // =========================================================================

        const double proximity =
            ::fin_ocr::chart::association::geometry::
                normalized_distance_score(
                    best_distance,
                    MAX_OBJECT_LABEL_DISTANCE
                );

        // =========================================================================
        // CONFIDENCE
        // =========================================================================

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
                    bar.confidence
                )
            );

        // =========================================================================
        // BUILD ASSOCIATION
        // =========================================================================

        ::fin_ocr::chart::association::ChartAssociation association{};

        association.label_index =
            best_label;

        association.object_kind =
            vertical
                ? ::fin_ocr::chart::association::
                      AssociatedObjectKind::COLUMN
                : ::fin_ocr::chart::association::
                      AssociatedObjectKind::BAR;

        association.object_index =
            static_cast<int>(
                bar_index
            );

        association.series_index =
            bar.series_index;

        association.category_index =
            label.category_index;

        association.stack_index =
            bar.stack_index;

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

        // =========================================================================
        // CENTRALIZED COMMIT
        // =========================================================================

        ::fin_ocr::chart::association::storage::
            append_association(
                associations,
                association
            );
    }
}

} // namespace fin_ocr::chart::association::object::bar
