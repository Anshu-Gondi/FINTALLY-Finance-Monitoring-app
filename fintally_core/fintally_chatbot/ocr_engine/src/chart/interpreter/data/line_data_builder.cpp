#include "fin_ocr/chart/interpreter/data/line_data_builder.hpp"

#include "fin_ocr/chart/interpreter/axis_value_mapper.hpp"
#include "fin_ocr/chart/interpreter/interpreter_lookup.hpp"

#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace fin_ocr::chart::interpreter::data::line {

// =============================================================================
// LINE DATA
// =============================================================================
//
// One ChartDataPoint is emitted per sampled path point.
//
// This preserves the underlying line geometry instead of collapsing a path
// into a single aggregate value.
//
// =============================================================================

std::vector<ChartDataPoint> build_line_data(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    const ::fin_ocr::chart::association::ChartAssociationResult& associations
)
{
    std::vector<ChartDataPoint> data;

    for (
        std::size_t path_index = 0;
        path_index < objects.paths.size();
        ++path_index
    ) {
        const ::fin_ocr::chart::object::ChartPath& path =
            objects.paths[path_index];

        for (
            const ::fin_ocr::chart::object::ChartPathPoint& point :
            path.points
        ) {
            ChartDataPoint value{};

            // -----------------------------------------------------------------
            // Object / series identity.
            // -----------------------------------------------------------------

            value.object_index =
                static_cast<int>(
                    path_index
                );

            value.series_index =
                path.series_index;

            value.series =
                lookup::series_name(
                    associations,
                    path.series_index
                );

            // -----------------------------------------------------------------
            // Map image coordinates into chart values.
            // -----------------------------------------------------------------

            double x_value =
                std::numeric_limits<double>::quiet_NaN();

            double y_value =
                std::numeric_limits<double>::quiet_NaN();

            const bool mapped =
                axis::path_point_values(
                    coordinates,
                    point,
                    x_value,
                    y_value
                );

            // -----------------------------------------------------------------
            // X value is represented as the category for the current
            // ChartDataPoint model.
            // -----------------------------------------------------------------

            value.category =
                mapped
                    ? std::to_string(
                          x_value
                      )
                    : std::string{};

            // -----------------------------------------------------------------
            // Y value.
            // -----------------------------------------------------------------

            value.value.value =
                y_value;

            value.value.valid =
                mapped;

            value.value.confidence =
                mapped
                    ? (
                        point.confidence *
                        path.confidence
                    )
                    : 0.0f;

            value.confidence =
                value.value.confidence;

            data.push_back(
                std::move(value)
            );
        }
    }

    return data;
}

} // namespace fin_ocr::chart::interpreter::data::line
