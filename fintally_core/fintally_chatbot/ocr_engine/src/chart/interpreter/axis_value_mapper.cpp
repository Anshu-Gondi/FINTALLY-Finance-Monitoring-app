#include "fin_ocr/chart/interpreter/axis_value_mapper.hpp"

#include "fin_ocr/chart/interpreter/interpreter_geometry.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <cmath>
#include <cstddef>
#include <limits>

namespace fin_ocr::chart::interpreter::axis {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr double MIN_SCALE_PIXELS =
    config::CHART_INTERPRETER_MIN_SCALE_PIXELS;

constexpr std::size_t MIN_NUMERIC_AXIS_TICKS =
    static_cast<std::size_t>(
        config::CHART_INTERPRETER_MIN_NUMERIC_AXIS_TICKS
    );

} // namespace

// =============================================================================
// NUMERIC TICK VALIDATION
// =============================================================================

bool has_numeric_ticks(
    const ::fin_ocr::chart::ChartAxis& axis
) noexcept
{
    std::size_t count = 0;

    for (
        const ::fin_ocr::chart::AxisTick& tick :
        axis.ticks
    ) {
        if (
            tick.has_numeric_value &&
            geometry::finite_value(
                tick.numeric_value
            )
        ) {
            ++count;
        }
    }

    return
        count >=
        MIN_NUMERIC_AXIS_TICKS;
}

// =============================================================================
// AXIS VALUE INTERPOLATION
// =============================================================================
//
// Interpolates only between actual numeric ticks surrounding the requested
// pixel.
//
// No extrapolation.
// No fabricated zero baseline.
//
// =============================================================================

bool interpolate_axis_value(
    const ::fin_ocr::chart::ChartAxis& axis,
    int pixel,
    double& value
) noexcept
{
    if (
        !axis.horizontal &&
        !axis.vertical
    ) {
        return false;
    }

    const ::fin_ocr::chart::AxisTick* lower =
        nullptr;

    const ::fin_ocr::chart::AxisTick* upper =
        nullptr;

    for (
        const ::fin_ocr::chart::AxisTick& tick :
        axis.ticks
    ) {
        if (
            !tick.has_numeric_value ||
            !geometry::finite_value(
                tick.numeric_value
            )
        ) {
            continue;
        }

        if (
            tick.pixel_position <=
            pixel
        ) {
            if (
                lower == nullptr ||
                tick.pixel_position >
                    lower->pixel_position
            ) {
                lower =
                    &tick;
            }
        }

        if (
            tick.pixel_position >=
            pixel
        ) {
            if (
                upper == nullptr ||
                tick.pixel_position <
                    upper->pixel_position
            ) {
                upper =
                    &tick;
            }
        }
    }

    // -------------------------------------------------------------------------
    // A valid interpolation requires two distinct numeric anchors.
    // -------------------------------------------------------------------------

    if (
        lower == nullptr ||
        upper == nullptr ||
        lower == upper
    ) {
        return false;
    }

    const int pixel_delta =
        upper->pixel_position -
        lower->pixel_position;

    if (
        pixel_delta == 0
    ) {
        return false;
    }

    const double alpha =
        static_cast<double>(
            pixel -
            lower->pixel_position
        ) /
        static_cast<double>(
            pixel_delta
        );

    if (
        !geometry::finite_value(alpha)
    ) {
        return false;
    }

    value =
        lower->numeric_value +
        (
            upper->numeric_value -
            lower->numeric_value
        ) *
        alpha;

    return
        geometry::finite_value(value);
}

// =============================================================================
// PATH POINT VALUES
// =============================================================================

bool path_point_values(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartPathPoint& point,
    double& x_value,
    double& y_value
) noexcept
{
    if (
        !coordinates.valid ||
        !has_numeric_ticks(
            coordinates.x_axis
        ) ||
        !has_numeric_ticks(
            coordinates.y_axis
        )
    ) {
        return false;
    }

    if (
        !interpolate_axis_value(
            coordinates.x_axis,
            point.x,
            x_value
        )
    ) {
        return false;
    }

    if (
        !interpolate_axis_value(
            coordinates.y_axis,
            point.y,
            y_value
        )
    ) {
        return false;
    }

    return
        geometry::finite_value(x_value) &&
        geometry::finite_value(y_value);
}

// =============================================================================
// PIXEL -> VALUE
// =============================================================================
//
// Returns NaN when a reliable value cannot be established.
//
// =============================================================================

double pixel_to_value(
    const ::fin_ocr::chart::ChartAxis& axis_value,
    int pixel
) noexcept
{
    double value =
        0.0;

    if (
        interpolate_axis_value(
            axis_value,
            pixel,
            value
        )
    ) {
        return value;
    }

    return
        std::numeric_limits<double>::quiet_NaN();
}

// =============================================================================
// SCALE ESTIMATION
// =============================================================================
//
// Estimates units per pixel between the first and last numeric ticks.
//
// This does not assume the axis starts at zero.
//
// =============================================================================

double estimate_scale(
    const ::fin_ocr::chart::ChartAxis& axis_value
) noexcept
{
    if (
        !has_numeric_ticks(
            axis_value
        )
    ) {
        return
            std::numeric_limits<double>::quiet_NaN();
    }

    const ::fin_ocr::chart::AxisTick* first =
        nullptr;

    const ::fin_ocr::chart::AxisTick* last =
        nullptr;

    for (
        const ::fin_ocr::chart::AxisTick& tick :
        axis_value.ticks
    ) {
        if (
            !tick.has_numeric_value ||
            !geometry::finite_value(
                tick.numeric_value
            )
        ) {
            continue;
        }

        if (
            first == nullptr ||
            tick.pixel_position <
                first->pixel_position
        ) {
            first =
                &tick;
        }

        if (
            last == nullptr ||
            tick.pixel_position >
                last->pixel_position
        ) {
            last =
                &tick;
        }
    }

    if (
        first == nullptr ||
        last == nullptr ||
        first == last
    ) {
        return
            std::numeric_limits<double>::quiet_NaN();
    }

    const int pixel_delta =
        last->pixel_position -
        first->pixel_position;

    if (
        std::abs(
            static_cast<double>(
                pixel_delta
            )
        ) <
        MIN_SCALE_PIXELS
    ) {
        return
            std::numeric_limits<double>::quiet_NaN();
    }

    const double value_delta =
        last->numeric_value -
        first->numeric_value;

    const double scale =
        value_delta /
        static_cast<double>(
            pixel_delta
        );

    return
        geometry::finite_value(scale)
            ? scale
            : std::numeric_limits<double>::quiet_NaN();
}

} // namespace fin_ocr::chart::interpreter::axis
