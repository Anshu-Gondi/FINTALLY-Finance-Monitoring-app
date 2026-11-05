#include "fin_ocr/chart/axis/axis_ticks.hpp"

#include "fin_ocr/chart/axis/axis_projection.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fin_ocr::chart::axis {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr int MAX_TICK_SEARCH_DISTANCE =
    config::CHART_MAX_TICK_SEARCH_DISTANCE;

constexpr int MIN_TICK_SPACING =
    config::CHART_MIN_TICK_SPACING;

constexpr int MAX_TICKS =
    config::CHART_MAX_TICKS;

} // namespace

// =============================================================================
// APPEND TICK
// =============================================================================

void append_tick(
    std::vector<AxisTick>& ticks,
    int position,
    float confidence
)
{
    if (
        position < 0 ||
        ticks.size() >=
            static_cast<std::size_t>(
                MAX_TICKS
            )
    ) {
        return;
    }

    // -------------------------------------------------------------------------
    // Enforce minimum spacing.
    //
    // Tick detection proceeds in axis order, so comparing against the last
    // accepted tick is sufficient.
    // -------------------------------------------------------------------------

    if (
        !ticks.empty() &&
        std::abs(
            position -
            ticks.back().pixel_position
        ) <
            MIN_TICK_SPACING
    ) {
        return;
    }

    AxisTick tick{};

    tick.pixel_position =
        position;

    tick.confidence =
        std::clamp(
            confidence,
            0.0f,
            1.0f
        );

    ticks.push_back(
        tick
    );
}

// =============================================================================
// HORIZONTAL TICKS
// =============================================================================

void detect_horizontal_ticks(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    ChartAxis& axis
)
{
    axis.ticks.clear();

    if (
        !valid_image(
            image,
            width,
            height,
            channels
        ) ||
        !axis.horizontal ||
        axis.end_x < axis.start_x
    ) {
        return;
    }

    const int axis_y =
        axis.start_y;

    if (
        axis_y < 0 ||
        axis_y >= height
    ) {
        return;
    }

    const int start_x =
        std::clamp(
            axis.start_x,
            0,
            width - 1
        );

    const int end_x =
        std::clamp(
            axis.end_x,
            start_x,
            width - 1
        );

    // -------------------------------------------------------------------------
    // Search vertically around the horizontal axis.
    // -------------------------------------------------------------------------

    const int y0 =
        std::max(
            0,
            axis_y -
                MAX_TICK_SEARCH_DISTANCE
        );

    const int y1 =
        std::min(
            height - 1,
            axis_y +
                MAX_TICK_SEARCH_DISTANCE
        );

    if (
        y0 > y1
    ) {
        return;
    }

    // -------------------------------------------------------------------------
    // Build X projection.
    //
    // Each X position receives the number of foreground pixels found around
    // the axis.
    //
    // The actual axis row is excluded so that the long horizontal axis itself
    // does not produce a signal everywhere.
    // -------------------------------------------------------------------------

    const std::size_t projection_size =
        static_cast<std::size_t>(
            end_x - start_x + 1
        );

    std::vector<int> projection(
        projection_size,
        0
    );

    for (
        int x = start_x;
        x <= end_x;
        ++x
    ) {
        int count = 0;

        for (
            int y = y0;
            y <= y1;
            ++y
        ) {
            if (
                y == axis_y
            ) {
                continue;
            }

            if (
                is_foreground(
                    image,
                    x,
                    y,
                    width,
                    channels
                )
            ) {
                ++count;
            }
        }

        projection[
            static_cast<std::size_t>(
                x - start_x
            )
        ] = count;
    }

    // -------------------------------------------------------------------------
    // Required signal.
    // -------------------------------------------------------------------------

    const int required_signal =
        std::max(
            1,
            MAX_TICK_SEARCH_DISTANCE / 4
        );

    // -------------------------------------------------------------------------
    // Convert projection signal runs into ticks.
    // -------------------------------------------------------------------------

    int run_start = -1;
    int peak = -1;
    int peak_value = 0;

    for (
        int x = start_x;
        x <= end_x;
        ++x
    ) {
        const int value =
            projection[
                static_cast<std::size_t>(
                    x - start_x
                )
            ];

        if (
            value >= required_signal
        ) {
            if (
                run_start < 0
            ) {
                run_start = x;
                peak = x;
                peak_value = value;

            } else if (
                value > peak_value
            ) {
                peak = x;
                peak_value = value;
            }

        } else if (
            run_start >= 0
        ) {
            append_tick(
                axis.ticks,
                peak
            );

            run_start = -1;
            peak = -1;
            peak_value = 0;
        }
    }

    // Flush final signal run.
    if (
        run_start >= 0
    ) {
        append_tick(
            axis.ticks,
            peak
        );
    }
}

// =============================================================================
// VERTICAL TICKS
// =============================================================================

void detect_vertical_ticks(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    ChartAxis& axis
)
{
    axis.ticks.clear();

    if (
        !valid_image(
            image,
            width,
            height,
            channels
        ) ||
        !axis.vertical ||
        axis.end_y < axis.start_y
    ) {
        return;
    }

    const int axis_x =
        axis.start_x;

    if (
        axis_x < 0 ||
        axis_x >= width
    ) {
        return;
    }

    const int start_y =
        std::clamp(
            axis.start_y,
            0,
            height - 1
        );

    const int end_y =
        std::clamp(
            axis.end_y,
            start_y,
            height - 1
        );

    // -------------------------------------------------------------------------
    // Search horizontally around the vertical axis.
    // -------------------------------------------------------------------------

    const int x0 =
        std::max(
            0,
            axis_x -
                MAX_TICK_SEARCH_DISTANCE
        );

    const int x1 =
        std::min(
            width - 1,
            axis_x +
                MAX_TICK_SEARCH_DISTANCE
        );

    if (
        x0 > x1
    ) {
        return;
    }

    // -------------------------------------------------------------------------
    // Build Y projection.
    // -------------------------------------------------------------------------

    const std::size_t projection_size =
        static_cast<std::size_t>(
            end_y - start_y + 1
        );

    std::vector<int> projection(
        projection_size,
        0
    );

    for (
        int y = start_y;
        y <= end_y;
        ++y
    ) {
        int count = 0;

        for (
            int x = x0;
            x <= x1;
            ++x
        ) {
            if (
                x == axis_x
            ) {
                continue;
            }

            if (
                is_foreground(
                    image,
                    x,
                    y,
                    width,
                    channels
                )
            ) {
                ++count;
            }
        }

        projection[
            static_cast<std::size_t>(
                y - start_y
            )
        ] = count;
    }

    // -------------------------------------------------------------------------
    // Required signal.
    // -------------------------------------------------------------------------

    const int required_signal =
        std::max(
            1,
            MAX_TICK_SEARCH_DISTANCE / 4
        );

    // -------------------------------------------------------------------------
    // Convert projection signal runs into ticks.
    // -------------------------------------------------------------------------

    int run_start = -1;
    int peak = -1;
    int peak_value = 0;

    for (
        int y = start_y;
        y <= end_y;
        ++y
    ) {
        const int value =
            projection[
                static_cast<std::size_t>(
                    y - start_y
                )
            ];

        if (
            value >= required_signal
        ) {
            if (
                run_start < 0
            ) {
                run_start = y;
                peak = y;
                peak_value = value;

            } else if (
                value > peak_value
            ) {
                peak = y;
                peak_value = value;
            }

        } else if (
            run_start >= 0
        ) {
            append_tick(
                axis.ticks,
                peak
            );

            run_start = -1;
            peak = -1;
            peak_value = 0;
        }
    }

    // Flush final signal run.
    if (
        run_start >= 0
    ) {
        append_tick(
            axis.ticks,
            peak
        );
    }
}

} // namespace fin_ocr::chart::axis
