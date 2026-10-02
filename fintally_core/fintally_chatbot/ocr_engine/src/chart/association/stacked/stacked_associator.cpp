#include "fin_ocr/chart/association/stacked/stacked_associator.hpp"

#include "fin_ocr/chart/association/association_geometry.hpp"

#include <algorithm>
#include <cstddef>

namespace fin_ocr::chart::association::stacked {

// =============================================================================
// STACKED BAR / COLUMN ASSOCIATION
// =============================================================================
//
// Geometry rule:
//
//     vertically stacked columns:
//         same X interval / substantial X overlap
//
//     horizontally stacked bars:
//         same Y interval / substantial Y overlap
//
// Stack index is assigned deterministically by visual ordering.
//
// This pass does NOT infer percentages or business values.
//
// =============================================================================

void associate_stacked_objects(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& objects
)
{
    // =========================================================================
    // MINIMUM BAR COUNT
    // =========================================================================

    if (
        objects.bars.size() < 2
    ) {
        return;
    }

    // The current algorithm does not inspect the coordinate system.
    (void)coordinates;

    // =========================================================================
    // RESET / PRESERVE STACK STATE
    // =========================================================================
    //
    // Preserve the current behavior of the legacy implementation.
    //
    // A negative stack index remains negative here and is assigned during the
    // comparison pass below.
    //
    // =========================================================================

    for (
        ::fin_ocr::chart::object::BarSegment& bar :
        objects.bars
    ) {
        if (
            bar.stack_index < 0
        ) {
            bar.stack_index =
                -1;
        }
    }

    // =========================================================================
    // BUILD STACK INDICES
    // =========================================================================

    for (
        std::size_t i = 0;
        i < objects.bars.size();
        ++i
    ) {
        ::fin_ocr::chart::object::BarSegment& current =
            objects.bars[i];

        if (
            current.stack_index < 0
        ) {
            current.stack_index =
                0;
        }

        const ::fin_ocr::chart::object::ChartRect& current_rect =
            current.bounds;

        const int current_width =
            current_rect.max_x -
            current_rect.min_x +
            1;

        const int current_height =
            current_rect.max_y -
            current_rect.min_y +
            1;

        const bool current_vertical =
            current_height >
            current_width;

        // =====================================================================
        // COMPARE AGAINST PREVIOUS BARS
        // =====================================================================

        for (
            std::size_t j = 0;
            j < i;
            ++j
        ) {
            ::fin_ocr::chart::object::BarSegment& previous =
                objects.bars[j];

            const ::fin_ocr::chart::object::ChartRect& previous_rect =
                previous.bounds;

            const int previous_width =
                previous_rect.max_x -
                previous_rect.min_x +
                1;

            const int previous_height =
                previous_rect.max_y -
                previous_rect.min_y +
                1;

            const bool previous_vertical =
                previous_height >
                previous_width;

            // -----------------------------------------------------------------
            // Orientation must match.
            // -----------------------------------------------------------------

            if (
                current_vertical !=
                previous_vertical
            ) {
                continue;
            }

            // =================================================================
            // VERTICAL STACK
            // =================================================================

            if (
                current_vertical
            ) {
                const double x_overlap =
                    ::fin_ocr::chart::association::geometry::
                        overlap_ratio_1d(
                            current_rect.min_x,
                            current_rect.max_x,
                            previous_rect.min_x,
                            previous_rect.max_x
                        );

                const bool vertical_touch =
                    current_rect.min_y <=
                        previous_rect.max_y + 2 &&
                    previous_rect.min_y <=
                        current_rect.max_y + 2;

                if (
                    x_overlap >= 0.70 &&
                    vertical_touch
                ) {
                    current.stack_index =
                        std::max(
                            current.stack_index,
                            previous.stack_index + 1
                        );
                }

            // =================================================================
            // HORIZONTAL STACK
            // =================================================================

            } else {
                const double y_overlap =
                    ::fin_ocr::chart::association::geometry::
                        overlap_ratio_1d(
                            current_rect.min_y,
                            current_rect.max_y,
                            previous_rect.min_y,
                            previous_rect.max_y
                        );

                const bool horizontal_touch =
                    current_rect.min_x <=
                        previous_rect.max_x + 2 &&
                    previous_rect.min_x <=
                        current_rect.max_x + 2;

                if (
                    y_overlap >= 0.70 &&
                    horizontal_touch
                ) {
                    current.stack_index =
                        std::max(
                            current.stack_index,
                            previous.stack_index + 1
                        );
                }
            }
        }
    }

    // =========================================================================
    // CLASSIFY STACKED SEGMENTS
    // =========================================================================
    //
    // Segments with stack_index > 0 are promoted to a deterministic series
    // index when no series index has already been supplied upstream.
    //
    // =========================================================================

    for (
        std::size_t i = 0;
        i < objects.bars.size();
        ++i
    ) {
        ::fin_ocr::chart::object::BarSegment& current =
            objects.bars[i];

        if (
            current.stack_index < 1
        ) {
            continue;
        }

        const int width =
            current.bounds.max_x -
            current.bounds.min_x +
            1;

        const int height =
            current.bounds.max_y -
            current.bounds.min_y +
            1;

        // ---------------------------------------------------------------------
        // Preserve the legacy behavior.
        //
        // Both branches currently assign stack_index in exactly the same way.
        // Do not silently change this during the modularization pass.
        // ---------------------------------------------------------------------

        if (
            height >
            width
        ) {
            current.series_index =
                current.series_index < 0
                    ? current.stack_index
                    : current.series_index;

        } else {
            current.series_index =
                current.series_index < 0
                    ? current.stack_index
                    : current.series_index;
        }
    }
}

} // namespace fin_ocr::chart::association::stacked
