#include "fin_ocr/chart/object/area_detector.hpp"

#include "fin_ocr/chart/object/object_components.hpp"
#include "fin_ocr/chart/object/object_geometry.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>

namespace fin_ocr::chart::object::area {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr int MIN_RECT_WIDTH =
    config::CHART_OBJECT_MIN_RECT_WIDTH;

constexpr int MIN_RECT_HEIGHT =
    config::CHART_OBJECT_MIN_RECT_HEIGHT;

constexpr std::size_t MAX_OBJECTS =
    config::CHART_OBJECT_MAX_OBJECTS;

constexpr double MIN_OBJECT_DENSITY =
    config::CHART_OBJECT_MIN_DENSITY;

// =============================================================================
// MINIMUM RELATIVE AREA
// =============================================================================
//
// A component must occupy a meaningful fraction of the plotting region before
// it is promoted to an area object.
//
// This preserves the threshold from the original implementation.
//
// =============================================================================

constexpr double MIN_RELATIVE_AREA = 0.015;

} // namespace

// =============================================================================
// AREA REGIONS
// =============================================================================

void detect_area_regions(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& result
)
{
    // =========================================================================
    // CONNECTED COMPONENTS
    // =========================================================================

    const std::vector<Component> components =
        find_components(
            chart_buffer,
            width,
            height,
            channels
        );

    // =========================================================================
    // COMPONENT CLASSIFICATION
    // =========================================================================

    for (
        const Component& component :
        components
    ) {
        if (
            result.generic_objects.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        // ---------------------------------------------------------------------
        // Basic geometric filtering.
        // ---------------------------------------------------------------------

        if (
            component.width() <
                MIN_RECT_WIDTH ||
            component.height() <
                MIN_RECT_HEIGHT
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Reject sparse components.
        // ---------------------------------------------------------------------

        if (
            component.density <
            MIN_OBJECT_DENSITY
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Component center.
        // ---------------------------------------------------------------------

        const int center_x =
            (
                component.min_x +
                component.max_x
            ) / 2;

        const int center_y =
            (
                component.min_y +
                component.max_y
            ) / 2;

        // ---------------------------------------------------------------------
        // Area regions should belong to the chart plot.
        // ---------------------------------------------------------------------

        if (
            !inside_plot(
                coordinates,
                center_x,
                center_y
            )
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Component area.
        // ---------------------------------------------------------------------

        const std::size_t component_area =
            static_cast<std::size_t>(
                component.width()
            ) *
            static_cast<std::size_t>(
                component.height()
            );

        // ---------------------------------------------------------------------
        // Determine reference plot area.
        //
        // For a valid coordinate system we use the actual detected plot
        // rectangle. Otherwise fall back to the complete image.
        // ---------------------------------------------------------------------

        const std::size_t plot_area =
            coordinates.valid
                ? static_cast<std::size_t>(
                      std::max(
                          1,
                          coordinates.plot_area.max_x -
                          coordinates.plot_area.min_x +
                          1
                      )
                  ) *
                  static_cast<std::size_t>(
                      std::max(
                          1,
                          coordinates.plot_area.max_y -
                          coordinates.plot_area.min_y +
                          1
                      )
                  )
                : static_cast<std::size_t>(
                      std::max(
                          width,
                          1
                      )
                  ) *
                  static_cast<std::size_t>(
                      std::max(
                          height,
                          1
                      )
                  );

        // ---------------------------------------------------------------------
        // Relative area.
        // ---------------------------------------------------------------------

        const double relative_area =
            static_cast<double>(
                component_area
            ) /
            static_cast<double>(
                std::max<std::size_t>(
                    plot_area,
                    1
                )
            );

        // ---------------------------------------------------------------------
        // Only materially large filled regions are promoted.
        // ---------------------------------------------------------------------

        if (
            relative_area <
            MIN_RELATIVE_AREA
        ) {
            continue;
        }

        // =========================================================================
        // BUILD OBJECT
        // =========================================================================

        ::fin_ocr::chart::object::ChartObject object{};

        object.kind =
            ::fin_ocr::chart::object::ChartObjectKind::AREA_REGION;

        object.bounds =
            to_rect(
                component
            );

        object.confidence =
            static_cast<float>(
                std::clamp(
                    component.density *
                        0.7 +
                    std::min(
                        relative_area *
                            8.0,
                        0.3
                    ),
                    0.0,
                    1.0
                )
            );

        result.generic_objects.push_back(
            object
        );
    }
}

} // namespace fin_ocr::chart::object::area
