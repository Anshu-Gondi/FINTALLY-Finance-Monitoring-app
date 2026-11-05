#include "fin_ocr/chart/interpreter/data/scatter_data_builder.hpp"

#include "fin_ocr/chart/interpreter/axis_value_mapper.hpp"
#include "fin_ocr/chart/interpreter/interpreter_geometry.hpp"
#include "fin_ocr/chart/interpreter/interpreter_lookup.hpp"

#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr::chart::interpreter::data::scatter {

// =============================================================================
// SCATTER DATA
// =============================================================================
//
// Converts detected scatter / bubble points into semantic ChartDataPoint
// records.
//
// X coordinate resolution:
//
//     1. detector-provided x_value
//     2. X-axis interpolation
//
// Y coordinate resolution:
//
//     1. detector-provided y_value
//     2. Y-axis interpolation
//
// No numeric values are fabricated.
//
// =============================================================================

std::vector<ChartDataPoint> build_scatter_data(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const ::fin_ocr::chart::association::ChartAssociationResult& associations
)
{
    std::vector<ChartDataPoint> data;

    data.reserve(
        objects.points.size()
    );

    for (
        std::size_t index = 0;
        index < objects.points.size();
        ++index
    ) {
        const ::fin_ocr::chart::object::ScatterPoint& point =
            objects.points[index];

        ChartDataPoint value{};

        // ---------------------------------------------------------------------
        // Object / series identity.
        // ---------------------------------------------------------------------

        value.object_index =
            static_cast<int>(
                index
            );

        value.series_index =
            point.series_index;

        value.series =
            lookup::series_name(
                associations,
                point.series_index
            );

        // ---------------------------------------------------------------------
        // X VALUE
        // ---------------------------------------------------------------------

        bool x_valid =
            false;

        double x_value =
            std::numeric_limits<double>::quiet_NaN();

        // Prefer an explicitly inferred detector value.
        if (
            point.has_x_value &&
            geometry::finite_value(
                point.x_value
            )
        ) {
            x_value =
                point.x_value;

            x_valid =
                true;

        // Otherwise interpolate from the X axis.
        } else if (
            axis::interpolate_axis_value(
                coordinates.x_axis,
                point.x,
                x_value
            )
        ) {
            x_valid =
                true;
        }

        if (
            x_valid &&
            geometry::finite_value(x_value)
        ) {
            value.category =
                std::to_string(
                    x_value
                );
        }

        // ---------------------------------------------------------------------
        // Y VALUE
        // ---------------------------------------------------------------------

        bool y_valid =
            false;

        double y_value =
            std::numeric_limits<double>::quiet_NaN();

        // Prefer an explicitly inferred detector value.
        if (
            point.has_y_value &&
            geometry::finite_value(
                point.y_value
            )
        ) {
            y_value =
                point.y_value;

            y_valid =
                true;

        // Otherwise interpolate from the Y axis.
        } else if (
            axis::interpolate_axis_value(
                coordinates.y_axis,
                point.y,
                y_value
            )
        ) {
            y_valid =
                true;
        }

        // ---------------------------------------------------------------------
        // Semantic value.
        // ---------------------------------------------------------------------

        value.value.value =
            y_value;

        value.value.valid =
            x_valid &&
            y_valid;

        value.value.confidence =
            value.value.valid
                ? point.confidence
                : 0.0f;

        value.confidence =
            point.confidence;

        data.push_back(
            std::move(value)
        );
    }

    return data;
}

} // namespace fin_ocr::chart::interpreter::data::scatter
