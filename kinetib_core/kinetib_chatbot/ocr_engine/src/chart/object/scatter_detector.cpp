#include "fin_ocr/chart/object/scatter_detector.hpp"

#include "fin_ocr/chart/object/object_components.hpp"
#include "fin_ocr/chart/object/object_geometry.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace fin_ocr::chart::object::scatter {

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

// =============================================================================
// SCATTER GEOMETRY LIMITS
// =============================================================================
//
// Compact components are treated as potential scatter markers.
//
// These preserve the limits from the original detector.
// =============================================================================

constexpr double MIN_ASPECT =
    0.25;

constexpr double MAX_ASPECT =
    4.0;

constexpr double COMPACT_MIN_ASPECT =
    0.5;

constexpr double COMPACT_MAX_ASPECT =
    2.0;

constexpr double BUBBLE_RADIUS_THRESHOLD =
    8.0;

} // namespace

// =============================================================================
// SCATTER / BUBBLE POINTS
// =============================================================================
//
// Detection flow:
//
//   1. Extract connected components.
//   2. Reject components below the minimum dimensions.
//   3. Calculate component center.
//   4. Require the center to lie inside the plotting region when one exists.
//   5. Reject strongly elongated components.
//   6. Estimate marker radius from bounding-box dimensions.
//   7. Classify larger markers as bubbles.
//   8. Emit ScatterPoint.
//
// No OCR is performed here.
//
// =============================================================================

void detect_scatter_points(
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

    const std::vector<
        ::fin_ocr::chart::object::Component
    > components =
        ::fin_ocr::chart::object::find_components(
            chart_buffer,
            width,
            height,
            channels
        );

    // =========================================================================
    // COMPONENT SCAN
    // =========================================================================

    for (
        const ::fin_ocr::chart::object::Component& component :
        components
    ) {
        if (
            result.points.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        // ---------------------------------------------------------------------
        // Minimum geometry.
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
        // Plot containment.
        // ---------------------------------------------------------------------

        if (
            !::fin_ocr::chart::object::inside_plot(
                coordinates,
                center_x,
                center_y
            )
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Aspect ratio.
        //
        // Very elongated components are more likely to represent:
        //
        //     - lines
        //     - bars
        //     - other chart geometry
        //
        // than scatter markers.
        // ---------------------------------------------------------------------

        const double aspect =
            component.aspect();

        if (
            aspect <
                MIN_ASPECT ||
            aspect >
                MAX_ASPECT
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Bounding-box area.
        // ---------------------------------------------------------------------

        const double area =
            static_cast<double>(
                component.width()
            ) *
            static_cast<double>(
                component.height()
            );

        if (
            area <= 0.0
        ) {
            continue;
        }

        // ---------------------------------------------------------------------
        // Marker radius.
        // ---------------------------------------------------------------------

        const double radius =
            0.25 *
            (
                static_cast<double>(
                    component.width()
                ) +
                static_cast<double>(
                    component.height()
                )
            );

        // =========================================================================
        // BUILD POINT
        // =========================================================================

        ::fin_ocr::chart::object::ScatterPoint point{};

        point.x =
            center_x;

        point.y =
            center_y;

        point.radius =
            std::max(
                1.0,
                radius
            );

        point.is_bubble =
            radius >=
            BUBBLE_RADIUS_THRESHOLD;

        point.series_index =
            -1;

        // ---------------------------------------------------------------------
        // Confidence.
        // ---------------------------------------------------------------------

        point.confidence =
            static_cast<float>(
                std::clamp(
                    component.density *
                        0.85 +
                    (
                        aspect >=
                            COMPACT_MIN_ASPECT &&
                        aspect <=
                            COMPACT_MAX_ASPECT
                            ? 0.15
                            : 0.0
                    ),
                    0.0,
                    1.0
                )
            );

        result.points.push_back(
            point
        );
    }
}

} // namespace fin_ocr::chart::object::scatter
