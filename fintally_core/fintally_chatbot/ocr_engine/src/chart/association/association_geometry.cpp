#include "fin_ocr/chart/association/association_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace fin_ocr::chart::association::geometry {

// =============================================================================
// RECTANGLE
// =============================================================================

bool valid_rect(
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept
{
    return
        min_x >= 0 &&
        min_y >= 0 &&
        max_x >= min_x &&
        max_y >= min_y;
}

// =============================================================================
// RECTANGLE CENTER
// =============================================================================

std::pair<int, int> rect_center(
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept
{
    return {
        min_x +
            (max_x - min_x) / 2,

        min_y +
            (max_y - min_y) / 2
    };
}

// =============================================================================
// CENTER DISTANCE
// =============================================================================

double center_distance(
    int ax,
    int ay,
    int bx,
    int by
) noexcept
{
    const double dx =
        static_cast<double>(ax) -
        static_cast<double>(bx);

    const double dy =
        static_cast<double>(ay) -
        static_cast<double>(by);

    return std::sqrt(
        dx * dx +
        dy * dy
    );
}

// =============================================================================
// POINT -> RECTANGLE DISTANCE
// =============================================================================

double point_to_rect_distance(
    int x,
    int y,
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept
{
    if (
        !valid_rect(
            min_x,
            min_y,
            max_x,
            max_y
        )
    ) {
        return std::numeric_limits<double>::infinity();
    }

    int dx = 0;
    int dy = 0;

    if (
        x < min_x
    ) {
        dx =
            min_x -
            x;

    } else if (
        x > max_x
    ) {
        dx =
            x -
            max_x;
    }

    if (
        y < min_y
    ) {
        dy =
            min_y -
            y;

    } else if (
        y > max_y
    ) {
        dy =
            y -
            max_y;
    }

    return std::sqrt(
        static_cast<double>(dx) *
            static_cast<double>(dx) +
        static_cast<double>(dy) *
            static_cast<double>(dy)
    );
}

// =============================================================================
// NORMALIZED DISTANCE
// =============================================================================

double normalized_distance_score(
    double distance,
    double max_distance
) noexcept
{
    if (
        !std::isfinite(distance) ||
        max_distance <= 0.0 ||
        distance >= max_distance
    ) {
        return 0.0;
    }

    return std::clamp(
        1.0 -
            (
                distance /
                max_distance
            ),
        0.0,
        1.0
    );
}

// =============================================================================
// HORIZONTAL OVERLAP
// =============================================================================

bool horizontal_overlap(
    int a_min_x,
    int a_max_x,
    int b_min_x,
    int b_max_x
) noexcept
{
    return
        a_min_x <= b_max_x &&
        b_min_x <= a_max_x;
}

// =============================================================================
// VERTICAL OVERLAP
// =============================================================================

bool vertical_overlap(
    int a_min_y,
    int a_max_y,
    int b_min_y,
    int b_max_y
) noexcept
{
    return
        a_min_y <= b_max_y &&
        b_min_y <= a_max_y;
}

// =============================================================================
// 1D OVERLAP RATIO
// =============================================================================

double overlap_ratio_1d(
    int a_min,
    int a_max,
    int b_min,
    int b_max
) noexcept
{
    const int overlap_min =
        std::max(
            a_min,
            b_min
        );

    const int overlap_max =
        std::min(
            a_max,
            b_max
        );

    if (
        overlap_max <
        overlap_min
    ) {
        return 0.0;
    }

    const int a_extent =
        a_max -
        a_min +
        1;

    const int b_extent =
        b_max -
        b_min +
        1;

    const int overlap =
        overlap_max -
        overlap_min +
        1;

    const int denominator =
        std::min(
            a_extent,
            b_extent
        );

    if (
        denominator <= 0
    ) {
        return 0.0;
    }

    return
        static_cast<double>(
            overlap
        ) /
        static_cast<double>(
            denominator
        );
}

// =============================================================================
// OBJECT CENTER
// =============================================================================

std::pair<int, int> object_center(
    const ::fin_ocr::chart::object::ChartRect& rect
) noexcept
{
    return rect_center(
        rect.min_x,
        rect.min_y,
        rect.max_x,
        rect.max_y
    );
}

// =============================================================================
// PATH BOUNDS
// =============================================================================

::fin_ocr::chart::object::ChartRect path_bounds(
    const ::fin_ocr::chart::object::ChartPath& path
) noexcept
{
    ::fin_ocr::chart::object::ChartRect result{};

    if (
        path.points.empty()
    ) {
        return result;
    }

    result.min_x =
        path.points.front().x;

    result.max_x =
        path.points.front().x;

    result.min_y =
        path.points.front().y;

    result.max_y =
        path.points.front().y;

    float confidence_sum =
        0.0f;

    std::size_t confidence_count =
        0;

    for (
        const ::fin_ocr::chart::object::ChartPathPoint& point :
        path.points
    ) {
        result.min_x =
            std::min(
                result.min_x,
                point.x
            );

        result.max_x =
            std::max(
                result.max_x,
                point.x
            );

        result.min_y =
            std::min(
                result.min_y,
                point.y
            );

        result.max_y =
            std::max(
                result.max_y,
                point.y
            );

        confidence_sum +=
            point.confidence;

        ++confidence_count;
    }

    result.confidence =
        confidence_count > 0
            ? confidence_sum /
              static_cast<float>(
                  confidence_count
              )
            : path.confidence;

    return result;
}

} // namespace fin_ocr::chart::association::geometry
