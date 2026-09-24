#include "fin_ocr/chart/object/bar_detector.hpp"

#include "fin_ocr/chart/object/object_components.hpp"
#include "fin_ocr/chart/object/object_geometry.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>

namespace fin_ocr::chart::object::bar {

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

constexpr double MIN_BAR_ASPECT =
    config::CHART_OBJECT_MIN_BAR_ASPECT;

constexpr double MAX_BAR_ASPECT =
    config::CHART_OBJECT_MAX_BAR_ASPECT;

// =============================================================================
// BAR COMPONENT CLASSIFICATION
// =============================================================================
//
// Determines whether a connected component is a plausible bar/column.
//
// `horizontal` is set to true when the component is considered a horizontal
// bar candidate.
//
// =============================================================================

[[nodiscard]]
bool classify_bar_component(
    const ::fin_ocr::chart::object::Component& component,
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    bool& horizontal
) noexcept
{
    horizontal = false;

    if (
        !::fin_ocr::chart::object::valid_rect(
            component.min_x,
            component.min_y,
            component.max_x,
            component.max_y
        )
    ) {
        return false;
    }

    const double aspect =
        component.aspect();

    const bool horizontal_bar =
        aspect >=
        1.0 /
        MAX_BAR_ASPECT;

    const bool vertical_bar =
        aspect <=
        MAX_BAR_ASPECT;

    if (
        !horizontal_bar &&
        !vertical_bar
    ) {
        return false;
    }

    if (
        coordinates.valid
    ) {
        ::fin_ocr::chart::object::ChartRect plot{
            coordinates.plot_area.min_x,
            coordinates.plot_area.min_y,
            coordinates.plot_area.max_x,
            coordinates.plot_area.max_y,
            coordinates.plot_area.confidence
        };

        const ::fin_ocr::chart::object::ChartRect component_rect{
            component.min_x,
            component.min_y,
            component.max_x,
            component.max_y,
            static_cast<float>(
                component.density
            )
        };

        const double plot_overlap =
            ::fin_ocr::chart::object::intersection_over_union(
                component_rect,
                plot
            );

        /*
         * Objects primarily outside the Cartesian plot are more likely to be:
         *
         *     legend
         *     title
         *     border
         *
         * than actual bars.
         */
        if (
            plot_overlap < 0.05
        ) {
            return false;
        }
    }

    horizontal =
        horizontal_bar &&
        (
            aspect >
            1.0
        );

    return true;
}

// =============================================================================
// AXIS VALUE INFERENCE
// =============================================================================
//
// Converts an image-space coordinate into an axis value when two surrounding
// ticks have recognized numeric values.
//
// Geometry alone must never invent a numeric value.
//
// =============================================================================

[[nodiscard]]
bool infer_axis_value(
    const ::fin_ocr::chart::ChartAxis& axis,
    int pixel_position,
    double& value
) noexcept
{
    if (
        axis.ticks.size() < 2
    ) {
        return false;
    }

    const ::fin_ocr::chart::AxisTick* left =
        nullptr;

    const ::fin_ocr::chart::AxisTick* right =
        nullptr;

    for (
        const ::fin_ocr::chart::AxisTick& tick :
        axis.ticks
    ) {
        if (
            !tick.has_numeric_value
        ) {
            continue;
        }

        if (
            tick.pixel_position <=
            pixel_position
        ) {
            if (
                left == nullptr ||
                tick.pixel_position >
                    left->pixel_position
            ) {
                left = &tick;
            }
        }

        if (
            tick.pixel_position >=
            pixel_position
        ) {
            if (
                right == nullptr ||
                tick.pixel_position <
                    right->pixel_position
            ) {
                right = &tick;
            }
        }
    }

    if (
        left == nullptr ||
        right == nullptr ||
        left == right ||
        right->pixel_position ==
            left->pixel_position
    ) {
        return false;
    }

    const double alpha =
        static_cast<double>(
            pixel_position -
            left->pixel_position
        ) /
        static_cast<double>(
            right->pixel_position -
            left->pixel_position
        );

    value =
        left->numeric_value +
        (
            right->numeric_value -
            left->numeric_value
        ) *
        alpha;

    return true;
}

// =============================================================================
// BAR VALUE INFERENCE
// =============================================================================

void infer_bar_value(
    ::fin_ocr::chart::object::BarSegment& bar,
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates
) noexcept
{
    if (
        !coordinates.y_axis.vertical ||
        coordinates.y_axis.ticks.size() < 2
    ) {
        return;
    }

    const ::fin_ocr::chart::object::ChartRect& bounds =
        bar.bounds;

    const int anchor_y =
        bar.negative
            ? bounds.max_y
            : bounds.min_y;

    double value = 0.0;

    if (
        infer_axis_value(
            coordinates.y_axis,
            anchor_y,
            value
        )
    ) {
        bar.inferred_value =
            value;

        bar.has_inferred_value =
            true;
    }
}

// =============================================================================
// DUPLICATE BAR CHECK
// =============================================================================
//
// Rejects a candidate when it substantially overlaps an already accepted bar.
//
// =============================================================================

[[nodiscard]]
bool duplicate_bar(
    const ::fin_ocr::chart::object::BarSegment& candidate,
    const std::vector<
        ::fin_ocr::chart::object::BarSegment
    >& existing
) noexcept
{
    for (
        const ::fin_ocr::chart::object::BarSegment& bar :
        existing
    ) {
        const double overlap =
            ::fin_ocr::chart::object::intersection_over_union(
                candidate.bounds,
                bar.bounds
            );

        if (
            overlap >= 0.80
        ) {
            return true;
        }
    }

    return false;
}

} // namespace

// =============================================================================
// BARS / COLUMNS
// =============================================================================

void detect_bars_and_columns(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    ::fin_ocr::chart::object::ChartObjectSet& result
)
{
    const std::vector<
        ::fin_ocr::chart::object::Component
    > components =
        ::fin_ocr::chart::object::find_components(
            chart_buffer,
            width,
            height,
            channels
        );

    for (
        const ::fin_ocr::chart::object::Component& component :
        components
    ) {
        if (
            result.bars.size() >=
            MAX_OBJECTS
        ) {
            break;
        }

        bool horizontal =
            false;

        if (
            !classify_bar_component(
                component,
                coordinates,
                horizontal
            )
        ) {
            continue;
        }

        const int rect_width =
            component.width();

        const int rect_height =
            component.height();

        if (
            rect_width <
                MIN_RECT_WIDTH ||
            rect_height <
                MIN_RECT_HEIGHT
        ) {
            continue;
        }

        /*
         * Very dense roughly rectangular connected components are the strongest
         * deterministic bar candidates.
         */
        if (
            component.density <
            MIN_OBJECT_DENSITY
        ) {
            continue;
        }

        ::fin_ocr::chart::object::BarSegment bar{};

        bar.bounds =
            ::fin_ocr::chart::object::to_rect(
                component
            );

        bar.confidence =
            static_cast<float>(
                std::clamp(
                    (
                        component.density *
                        0.55 +
                        std::min(
                            component.aspect(),
                            MAX_BAR_ASPECT
                        ) /
                        MAX_BAR_ASPECT *
                        0.20 +
                        0.25
                    ),
                    0.0,
                    1.0
                )
            );

        bar.negative =
            coordinates.valid &&
            coordinates.y_axis.vertical &&
            component.max_y >
                coordinates.x_axis.start_y;

        /*
         * A wide rectangle crossing many X positions is a horizontal BAR.
         *
         * A tall rectangle with narrow width is a COLUMN.
         */
        if (
            horizontal &&
            rect_width >
                rect_height
        ) {
            result.bars.push_back(
                bar
            );

        } else if (
            rect_height >
            rect_width
        ) {
            result.bars.push_back(
                bar
            );

        } else {

            /*
             * Nearly square components are retained only when they sit inside
             * the Cartesian plotting area. This avoids turning text/legend
             * blocks into bars.
             */
            if (
                ::fin_ocr::chart::object::inside_plot(
                    coordinates,
                    (
                        component.min_x +
                        component.max_x
                    ) / 2,
                    (
                        component.min_y +
                        component.max_y
                    ) / 2
                )
            ) {
                result.bars.push_back(
                    bar
                );
            }
        }

        if (
            !result.bars.empty()
        ) {
            ::fin_ocr::chart::object::BarSegment& inserted =
                result.bars.back();

            infer_bar_value(
                inserted,
                coordinates
            );
        }
    }

    // =========================================================================
    // DUPLICATE FILTER
    // =========================================================================

    std::vector<
        ::fin_ocr::chart::object::BarSegment
    > filtered;

    filtered.reserve(
        result.bars.size()
    );

    for (
        const ::fin_ocr::chart::object::BarSegment& bar :
        result.bars
    ) {
        if (
            duplicate_bar(
                bar,
                filtered
            )
        ) {
            continue;
        }

        filtered.push_back(
            bar
        );
    }

    result.bars.swap(
        filtered
    );
}

} // namespace fin_ocr::chart::object::bar
