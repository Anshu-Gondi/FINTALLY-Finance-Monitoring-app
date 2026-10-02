#include "fin_ocr/chart/interpreter/data/bar_data_builder.hpp"

#include "fin_ocr/chart/interpreter/axis_value_mapper.hpp"
#include "fin_ocr/chart/interpreter/interpreter_geometry.hpp"
#include "fin_ocr/chart/interpreter/interpreter_lookup.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace fin_ocr::chart::interpreter::data::bar {

// =============================================================================
// BAR DATA
// =============================================================================
//
// Converts detected bar/column geometry into semantic ChartDataPoint records.
//
// Value resolution order:
//
//     1. detector-provided inferred value
//     2. numeric Y-axis interpolation
//     3. invalid value
//
// Geometry never fabricates a monetary value.
//
// =============================================================================

std::vector<ChartDataPoint> build_bar_data(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const ::fin_ocr::chart::association::ChartAssociationResult& associations
)
{
    std::vector<ChartDataPoint> data;

    data.reserve(
        std::min<std::size_t>(
            objects.bars.size(),
            4096
        )
    );

    for (
        std::size_t index = 0;
        index < objects.bars.size();
        ++index
    ) {

        const ::fin_ocr::chart::object::BarSegment& bar =
            objects.bars[index];

        ChartDataPoint point{};

        // ---------------------------------------------------------------------
        // Object identity.
        // ---------------------------------------------------------------------

        point.object_index =
            static_cast<int>(
                index
            );

        point.category_index =
            bar.category_index;

        point.series_index =
            bar.series_index;

        // ---------------------------------------------------------------------
        // Semantic names.
        // ---------------------------------------------------------------------

        point.category =
            lookup::category_name(
                associations,
                bar.category_index
            );

        point.series =
            lookup::series_name(
                associations,
                bar.series_index
            );

        // ---------------------------------------------------------------------
        // Resolve value.
        // ---------------------------------------------------------------------

        double value =
            std::numeric_limits<double>::quiet_NaN();

        bool value_valid =
            false;

        // ---------------------------------------------------------------------
        // Prefer a detector-provided numeric value.
        // ---------------------------------------------------------------------

        if (
            bar.has_inferred_value &&
            geometry::finite_value(
                bar.inferred_value
            )
        ) {

            value =
                bar.inferred_value;

            value_valid =
                true;

        // ---------------------------------------------------------------------
        // Otherwise map the bar geometry through the Y axis.
        // ---------------------------------------------------------------------

        } else if (
            coordinates.y_axis.vertical
        ) {

            const ::fin_ocr::chart::object::ChartRect& bounds =
                bar.bounds;

            /*
             * Preserve legacy baseline selection:
             *
             *     negative bar -> max_y
             *     positive bar -> min_y
             *
             * The actual numeric value is obtained only through interpolation
             * between recognized numeric axis ticks.
             */
            const int pixel =
                bar.negative
                    ? bounds.max_y
                    : bounds.min_y;

            value_valid =
                axis::interpolate_axis_value(
                    coordinates.y_axis,
                    pixel,
                    value
                );
        }

        // ---------------------------------------------------------------------
        // Populate value.
        // ---------------------------------------------------------------------

        point.value.value =
            value;

        point.value.valid =
            value_valid;

        point.value.confidence =
            value_valid
                ? (
                    bar.confidence *
                    0.85f
                )
                : 0.0f;

        // ---------------------------------------------------------------------
        // Point confidence.
        // ---------------------------------------------------------------------

        point.confidence =
            std::max(
                point.value.confidence,
                bar.confidence
            );

        data.push_back(
            std::move(point)
        );
    }

    return data;
}

} // namespace fin_ocr::chart::interpreter::data::bar
