#include "fin_ocr/chart/axis/axis_thickness.hpp"

#include "fin_ocr/chart/axis/axis_projection.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstdint>

namespace fin_ocr::chart {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr int AXIS_SEARCH_THICKNESS =
    config::CHART_AXIS_SEARCH_THICKNESS;

constexpr double MIN_AXIS_DENSITY =
    config::CHART_MIN_AXIS_DENSITY;

} // namespace

// =============================================================================
// HORIZONTAL AXIS THICKNESS
// =============================================================================
//
// The supplied y coordinate represents the center row of the detected
// horizontal axis.
//
// We inspect symmetric rows around it:
//
//                  above
//                    |
//                    v
//             ----------------
//             ----------------  <- axis
//             ----------------
//                    ^
//                    |
//                  below
//
// If both rows retain sufficient horizontal foreground coverage, they are
// considered part of the axis thickness.
//
// The result is always odd because the supplied y row is treated as the
// center of the axis.
//
// =============================================================================

[[nodiscard]]
int estimate_horizontal_axis_thickness(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    int y
) noexcept
{
    if (
        !axis::valid_image(
            image,
            width,
            height,
            channels
        ) ||
        y < 0 ||
        y >= height
    ) {
        return 0;
    }

    int thickness = 1;

    for (
        int delta = 1;
        delta <= AXIS_SEARCH_THICKNESS;
        ++delta
    ) {
        const int above =
            y - delta;

        const int below =
            y + delta;

        if (
            above < 0 ||
            below >= height
        ) {
            break;
        }

        const double above_coverage =
            axis::horizontal_span(
                image,
                width,
                above,
                channels
            ).coverage(width);

        const double below_coverage =
            axis::horizontal_span(
                image,
                width,
                below,
                channels
            ).coverage(width);

        const double required_coverage =
            MIN_AXIS_DENSITY * 0.5;

        if (
            above_coverage >= required_coverage &&
            below_coverage >= required_coverage
        ) {
            thickness =
                delta * 2 + 1;
        }
    }

    return thickness;
}

// =============================================================================
// VERTICAL AXIS THICKNESS
// =============================================================================
//
// The supplied x coordinate represents the center column of the detected
// vertical axis.
//
// We inspect symmetric columns around it:
//
//                  |
//                  |
//                  |
//             -----+-----
//                  |
//                  |
//                  |
//
// If both columns retain sufficient vertical foreground coverage, they are
// considered part of the axis thickness.
//
// =============================================================================

[[nodiscard]]
int estimate_vertical_axis_thickness(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    int x
) noexcept
{
    if (
        !axis::valid_image(
            image,
            width,
            height,
            channels
        ) ||
        x < 0 ||
        x >= width
    ) {
        return 0;
    }

    int thickness = 1;

    for (
        int delta = 1;
        delta <= AXIS_SEARCH_THICKNESS;
        ++delta
    ) {
        const int left =
            x - delta;

        const int right =
            x + delta;

        if (
            left < 0 ||
            right >= width
        ) {
            break;
        }

        const double left_coverage =
            axis::vertical_span(
                image,
                width,
                height,
                left,
                channels
            ).coverage(height);

        const double right_coverage =
            axis::vertical_span(
                image,
                width,
                height,
                right,
                channels
            ).coverage(height);

        const double required_coverage =
            MIN_AXIS_DENSITY * 0.5;

        if (
            left_coverage >= required_coverage &&
            right_coverage >= required_coverage
        ) {
            thickness =
                delta * 2 + 1;
        }
    }

    return thickness;
}

} // namespace fin_ocr::chart
