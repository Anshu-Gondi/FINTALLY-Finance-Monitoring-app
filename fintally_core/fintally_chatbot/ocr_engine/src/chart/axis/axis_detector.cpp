#include "fin_ocr/chart/axis/axis_detector.hpp"

#include "fin_ocr/chart/axis/axis_projection.hpp"
#include "fin_ocr/chart/axis/axis_scanner.hpp"
#include "fin_ocr/chart/axis/axis_thickness.hpp"
#include "fin_ocr/chart/axis/axis_ticks.hpp"
#include "fin_ocr/chart/coordinate/plot_area.hpp"

#include <algorithm>

namespace fin_ocr::chart {

// =============================================================================
// COMPLETE COORDINATE SYSTEM
// =============================================================================

ChartCoordinateSystem ChartAxisDetector::detect(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels
) const
{
    ChartCoordinateSystem result{};

    if (
        !axis::valid_image(
            chart_buffer,
            width,
            height,
            channels
        )
    ) {
        return result;
    }

    // =========================================================================
    // AXIS SCANNING
    // =========================================================================

    const axis::AxisScanner scanner{};

    result.x_axis =
        scanner.detect_horizontal(
            chart_buffer,
            width,
            height,
            channels
        );

    result.y_axis =
        scanner.detect_vertical(
            chart_buffer,
            width,
            height,
            channels
        );

    // =========================================================================
    // AXIS THICKNESS
    // =========================================================================

    if (
        result.x_axis.horizontal
    ) {
        result.x_axis.thickness =
            estimate_horizontal_axis_thickness(
                chart_buffer,
                width,
                height,
                channels,
                result.x_axis.start_y
            );
    }

    if (
        result.y_axis.vertical
    ) {
        result.y_axis.thickness =
            estimate_vertical_axis_thickness(
                chart_buffer,
                width,
                height,
                channels,
                result.y_axis.start_x
            );
    }

    // =========================================================================
    // TICKS
    // =========================================================================

    if (
        result.x_axis.horizontal
    ) {
        axis::detect_horizontal_ticks(
            chart_buffer,
            width,
            height,
            channels,
            result.x_axis
        );
    }

    if (
        result.y_axis.vertical
    ) {
        axis::detect_vertical_ticks(
            chart_buffer,
            width,
            height,
            channels,
            result.y_axis
        );
    }

    // =========================================================================
    // PLOT AREA
    // =========================================================================

    result.plot_area =
        coordinate::detect_plot_area(
            result.x_axis,
            result.y_axis,
            width,
            height
        );

    // =========================================================================
    // VALIDITY
    // =========================================================================

    const bool has_x =
        result.x_axis.horizontal &&
        result.x_axis.confidence > 0.0f;

    const bool has_y =
        result.y_axis.vertical &&
        result.y_axis.confidence > 0.0f;

    result.valid =
        has_x &&
        has_y &&
        result.plot_area.max_x >
            result.plot_area.min_x &&
        result.plot_area.max_y >
            result.plot_area.min_y;

    // =========================================================================
    // CONFIDENCE
    // =========================================================================

    if (
        result.valid
    ) {
        result.confidence =
            (
                result.x_axis.confidence +
                result.y_axis.confidence +
                result.plot_area.confidence
            ) /
            3.0f;

    } else if (
        has_x ||
        has_y
    ) {
        result.confidence =
            std::max(
                result.x_axis.confidence,
                result.y_axis.confidence
            ) *
            0.5f;
    }

    return result;
}

} // namespace fin_ocr::chart
