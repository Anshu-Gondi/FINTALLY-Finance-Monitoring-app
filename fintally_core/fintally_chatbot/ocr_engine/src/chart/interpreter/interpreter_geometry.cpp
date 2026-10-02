#include "fin_ocr/chart/interpreter/interpreter_geometry.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fin_ocr::chart::interpreter::geometry {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr double STACK_X_TOLERANCE =
    config::CHART_INTERPRETER_STACK_X_TOLERANCE;

constexpr double STACK_Y_TOLERANCE =
    config::CHART_INTERPRETER_STACK_Y_TOLERANCE;

} // namespace

// =============================================================================
// RECTANGLE VALIDATION
// =============================================================================

bool valid_rect(
    const ::fin_ocr::chart::object::ChartRect& rect
) noexcept
{
    return
        rect.max_x >= rect.min_x &&
        rect.max_y >= rect.min_y;
}

// =============================================================================
// RECTANGLE WIDTH
// =============================================================================

int rect_width(
    const ::fin_ocr::chart::object::ChartRect& rect
) noexcept
{
    if (
        !valid_rect(rect)
    ) {
        return 0;
    }

    return
        rect.max_x -
        rect.min_x +
        1;
}

// =============================================================================
// RECTANGLE HEIGHT
// =============================================================================

int rect_height(
    const ::fin_ocr::chart::object::ChartRect& rect
) noexcept
{
    if (
        !valid_rect(rect)
    ) {
        return 0;
    }

    return
        rect.max_y -
        rect.min_y +
        1;
}

// =============================================================================
// RECTANGLE CENTER
// =============================================================================

std::pair<int, int> rect_center(
    const ::fin_ocr::chart::object::ChartRect& rect
) noexcept
{
    return {
        rect.min_x +
            (rect.max_x - rect.min_x) / 2,

        rect.min_y +
            (rect.max_y - rect.min_y) / 2
    };
}

// =============================================================================
// FINITE VALUE
// =============================================================================

bool finite_value(
    double value
) noexcept
{
    return
        std::isfinite(value);
}

// =============================================================================
// POINT INSIDE PLOT
// =============================================================================

bool inside_plot(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    int x,
    int y
) noexcept
{
    // Preserve legacy behavior:
    //
    // If the coordinate system itself is not valid, do not reject the point
    // purely on plot-area bounds.
    if (
        !coordinates.valid
    ) {
        return true;
    }

    return
        x >= coordinates.plot_area.min_x &&
        x <= coordinates.plot_area.max_x &&
        y >= coordinates.plot_area.min_y &&
        y <= coordinates.plot_area.max_y;
}

// =============================================================================
// SAME X
// =============================================================================
//
// Used for vertical stacked columns.
//
// =============================================================================

bool near_same_x(
    const ::fin_ocr::chart::object::ChartRect& a,
    const ::fin_ocr::chart::object::ChartRect& b
) noexcept
{
    const auto [ax, ay] =
        rect_center(a);

    const auto [bx, by] =
        rect_center(b);

    (void)ay;
    (void)by;

    return
        std::abs(
            static_cast<double>(
                ax - bx
            )
        ) <=
        STACK_X_TOLERANCE;
}

// =============================================================================
// SAME Y
// =============================================================================
//
// Used for horizontal stacked bars.
//
// =============================================================================

bool near_same_y(
    const ::fin_ocr::chart::object::ChartRect& a,
    const ::fin_ocr::chart::object::ChartRect& b
) noexcept
{
    const auto [ax, ay] =
        rect_center(a);

    const auto [bx, by] =
        rect_center(b);

    (void)ax;
    (void)bx;

    return
        std::abs(
            static_cast<double>(
                ay - by
            )
        ) <=
        STACK_Y_TOLERANCE;
}

// =============================================================================
// BAR ORIENTATION
// =============================================================================

bool is_vertical_bar(
    const ::fin_ocr::chart::object::BarSegment& bar
) noexcept
{
    return
        rect_height(
            bar.bounds
        ) >
        rect_width(
            bar.bounds
        );
}

// =============================================================================
// COMPATIBLE STACK RELATIONSHIP
// =============================================================================
//
// Two bars/columns are compatible when:
//
//     1. They have the same orientation.
//     2. Vertical columns have approximately the same X center and touch in Y.
//     3. Horizontal bars have approximately the same Y center and touch in X.
//
// This preserves the geometry-only semantics of the legacy interpreter.
//
// =============================================================================

bool stack_compatible(
    const ::fin_ocr::chart::object::BarSegment& a,
    const ::fin_ocr::chart::object::BarSegment& b
) noexcept
{
    const ::fin_ocr::chart::object::ChartRect& ar =
        a.bounds;

    const ::fin_ocr::chart::object::ChartRect& br =
        b.bounds;

    const int aw =
        rect_width(ar);

    const int bw =
        rect_width(br);

    const int ah =
        rect_height(ar);

    const int bh =
        rect_height(br);

    if (
        aw <= 0 ||
        bw <= 0 ||
        ah <= 0 ||
        bh <= 0
    ) {
        return false;
    }

    const bool a_vertical =
        is_vertical_bar(a);

    const bool b_vertical =
        is_vertical_bar(b);

    if (
        a_vertical !=
        b_vertical
    ) {
        return false;
    }

    // =========================================================================
    // VERTICAL COLUMNS
    // =========================================================================

    if (
        a_vertical
    ) {
        if (
            !near_same_x(
                ar,
                br
            )
        ) {
            return false;
        }

        const bool touching =
            std::abs(
                ar.max_y -
                br.min_y
            ) <= 2 ||
            std::abs(
                br.max_y -
                ar.min_y
            ) <= 2;

        return touching;
    }

    // =========================================================================
    // HORIZONTAL BARS
    // =========================================================================

    if (
        !near_same_y(
            ar,
            br
        )
    ) {
        return false;
    }

    const bool touching =
        std::abs(
            ar.max_x -
            br.min_x
        ) <= 2 ||
        std::abs(
            br.max_x -
            ar.min_x
        ) <= 2;

    return touching;
}

} // namespace fin_ocr::chart::interpreter::geometry
