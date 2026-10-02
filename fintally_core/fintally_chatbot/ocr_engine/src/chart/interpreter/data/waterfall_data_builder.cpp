#include "fin_ocr/chart/interpreter/data/waterfall_data_builder.hpp"

#include "fin_ocr/chart/interpreter/axis_value_mapper.hpp"
#include "fin_ocr/chart/interpreter/interpreter_geometry.hpp"
#include "fin_ocr/chart/interpreter/interpreter_lookup.hpp"

#include <cstddef>
#include <limits>
#include <utility>

namespace fin_ocr::chart::interpreter::data::waterfall {

// =============================================================================
// WATERFALL DATA
// =============================================================================
//
// Converts detected waterfall steps into semantic ChartDataPoint records.
//
// Value resolution:
//
//     1. Explicit inferred delta, when valid.
//     2. Y-axis interpolation from the step geometry.
//     3. Invalid value.
//
// No value is fabricated when neither source is available.
//
// =============================================================================

std::vector<ChartDataPoint> build_waterfall_data(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const ::fin_ocr::chart::association::ChartAssociationResult& associations
)
{
    std::vector<ChartDataPoint> data;

    data.reserve(
        objects.waterfall_steps.size()
    );

    for (
        std::size_t index = 0;
        index < objects.waterfall_steps.size();
        ++index
    ) {
        const ::fin_ocr::chart::object::WaterfallStep& step =
            objects.waterfall_steps[index];

        ChartDataPoint point{};

        // ---------------------------------------------------------------------
        // Object / category identity.
        // ---------------------------------------------------------------------

        point.object_index =
            static_cast<int>(
                index
            );

        point.category_index =
            step.category_index;

        point.category =
            lookup::category_name(
                associations,
                step.category_index
            );

        // ---------------------------------------------------------------------
        // Resolve value.
        // ---------------------------------------------------------------------

        double value =
            std::numeric_limits<double>::quiet_NaN();

        bool valid =
            false;

        // ---------------------------------------------------------------------
        // Prefer the detector-provided delta.
        // ---------------------------------------------------------------------

        if (
            step.has_delta &&
            geometry::finite_value(
                step.delta
            )
        ) {
            value =
                step.delta;

            valid =
                true;

        // ---------------------------------------------------------------------
        // Otherwise map the step geometry through the Y axis.
        // ---------------------------------------------------------------------

        } else if (
            coordinates.y_axis.vertical
        ) {
            const int y =
                (
                    step.bounds.min_y +
                    step.bounds.max_y
                ) / 2;

            valid =
                axis::interpolate_axis_value(
                    coordinates.y_axis,
                    y,
                    value
                );
        }

        // ---------------------------------------------------------------------
        // Populate value.
        // ---------------------------------------------------------------------

        point.value.value =
            value;

        point.value.valid =
            valid;

        point.value.confidence =
            valid
                ? step.confidence
                : 0.0f;

        point.confidence =
            step.confidence;

        data.push_back(
            std::move(point)
        );
    }

    return data;
}

} // namespace fin_ocr::chart::interpreter::data::waterfall
