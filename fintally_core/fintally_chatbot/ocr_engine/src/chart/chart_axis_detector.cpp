#include "fin_ocr/chart/chart_axis_detector.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace fin_ocr::chart {

// =============================================================================
// INTERNAL AXIS CANDIDATE
// =============================================================================
//
// Implementation-only structure.
// It is deliberately not exposed through the public header because callers
// only need the final ChartAxis representation.
//
// =============================================================================

struct AxisCandidate {

    int position = -1;

    int start = 0;
    int end = 0;

    double coverage = 0.0;
    double density = 0.0;
    double score = 0.0;

    [[nodiscard]]
    bool valid() const noexcept {
        return
            position >= 0 &&
            end >= start;
    }
};

namespace {

// =============================================================================
// CENTRAL CONFIG
// =============================================================================

constexpr std::uint8_t FOREGROUND_THRESHOLD =
    config::CHART_FOREGROUND_THRESHOLD;

constexpr int MIN_AXIS_LENGTH_PIXELS =
    config::CHART_MIN_AXIS_LENGTH_PIXELS;

constexpr int AXIS_SEARCH_THICKNESS =
    config::CHART_AXIS_SEARCH_THICKNESS;

constexpr double MIN_HORIZONTAL_COVERAGE =
    config::CHART_MIN_HORIZONTAL_AXIS_COVERAGE;

constexpr double MIN_VERTICAL_COVERAGE =
    config::CHART_MIN_VERTICAL_AXIS_COVERAGE;

constexpr double MIN_AXIS_DENSITY =
    config::CHART_MIN_AXIS_DENSITY;

constexpr int MAX_TICK_SEARCH_DISTANCE =
    config::CHART_MAX_TICK_SEARCH_DISTANCE;

constexpr int MIN_TICK_SPACING =
    config::CHART_MIN_TICK_SPACING;

constexpr int MAX_TICKS =
    config::CHART_MAX_TICKS;

constexpr int EDGE_MARGIN =
    config::CHART_AXIS_EDGE_MARGIN;

// =============================================================================
// VALIDATION
// =============================================================================

[[nodiscard]]
bool valid_image(
    const std::uint8_t* image,
    int width,
    int height,
    int channels
) noexcept
{
    return
        image != nullptr &&
        width > 0 &&
        height > 0 &&
        channels > 0;
}

// =============================================================================
// PIXEL OFFSET
// =============================================================================

[[nodiscard]]
bool safe_pixel_offset(
    int x,
    int y,
    int width,
    int channels,
    std::size_t& offset
) noexcept
{
    if (
        x < 0 ||
        y < 0 ||
        width <= 0 ||
        channels <= 0
    ) {
        return false;
    }

    const std::size_t sx =
        static_cast<std::size_t>(x);

    const std::size_t sy =
        static_cast<std::size_t>(y);

    const std::size_t sw =
        static_cast<std::size_t>(width);

    const std::size_t sc =
        static_cast<std::size_t>(channels);

    if (
        sy >
        std::numeric_limits<std::size_t>::max() / sw
    ) {
        return false;
    }

    const std::size_t pixel =
        sy * sw + sx;

    if (
        pixel >
        std::numeric_limits<std::size_t>::max() / sc
    ) {
        return false;
    }

    offset =
        pixel * sc;

    return true;
}

// =============================================================================
// FOREGROUND
// =============================================================================

[[nodiscard]]
inline bool is_foreground(
    const std::uint8_t* image,
    int x,
    int y,
    int width,
    int channels
) noexcept
{
    std::size_t offset = 0;

    if (
        image == nullptr ||
        !safe_pixel_offset(
            x,
            y,
            width,
            channels,
            offset
        )
    ) {
        return false;
    }

    /*
     * ChartColorIsolator representation:
     *
     *     channel 0 = saturation
     *     channel 1 = OCR foreground strength
     *     channel 2 = chroma
     *
     * Ordinary one-channel masks:
     *
     *     channel 0 = signal
     */
    const std::uint8_t value =
        channels >= 3
            ? image[offset + 1]
            : image[offset];

    return
        value >= FOREGROUND_THRESHOLD;
}

// =============================================================================
// SPAN STATISTICS
// =============================================================================

struct SpanStats {

    int first = -1;
    int last = -1;

    std::size_t active = 0;

    [[nodiscard]]
    int extent() const noexcept
    {
        if (
            first < 0 ||
            last < first
        ) {
            return 0;
        }

        return
            last -
            first +
            1;
    }

    [[nodiscard]]
    double coverage(
        int total
    ) const noexcept
    {
        const int span =
            extent();

        if (
            total <= 0 ||
            span <= 0
        ) {
            return 0.0;
        }

        return
            static_cast<double>(span) /
            static_cast<double>(total);
    }

    [[nodiscard]]
    double density() const noexcept
    {
        const int span =
            extent();

        if (
            span <= 0
        ) {
            return 0.0;
        }

        return
            static_cast<double>(active) /
            static_cast<double>(span);
    }
};

// =============================================================================
// HORIZONTAL SPAN
// =============================================================================

[[nodiscard]]
SpanStats horizontal_span(
    const std::uint8_t* image,
    int width,
    int y,
    int channels
) noexcept
{
    SpanStats stats{};

    if (
        image == nullptr ||
        width <= 0 ||
        y < 0 ||
        channels <= 0
    ) {
        return stats;
    }

    for (
        int x = 0;
        x < width;
        ++x
    ) {

        if (
            !is_foreground(
                image,
                x,
                y,
                width,
                channels
            )
        ) {
            continue;
        }

        ++stats.active;

        if (
            stats.first < 0
        ) {
            stats.first = x;
        }

        stats.last = x;
    }

    return stats;
}

// =============================================================================
// VERTICAL SPAN
// =============================================================================

[[nodiscard]]
SpanStats vertical_span(
    const std::uint8_t* image,
    int width,
    int height,
    int x,
    int channels
) noexcept
{
    SpanStats stats{};

    if (
        image == nullptr ||
        width <= 0 ||
        height <= 0 ||
        x < 0 ||
        x >= width ||
        channels <= 0
    ) {
        return stats;
    }

    for (
        int y = 0;
        y < height;
        ++y
    ) {

        if (
            !is_foreground(
                image,
                x,
                y,
                width,
                channels
            )
        ) {
            continue;
        }

        ++stats.active;

        if (
            stats.first < 0
        ) {
            stats.first = y;
        }

        stats.last = y;
    }

    return stats;
}

// =============================================================================
// AXIS SCORE
// =============================================================================

[[nodiscard]]
double axis_score(
    const SpanStats& stats,
    int total_length
) noexcept
{
    const double coverage =
        stats.coverage(
            total_length
        );

    const double density =
        stats.density();

    return
        coverage * 0.70 +
        density * 0.30;
}

// =============================================================================
// APPEND TICK
// =============================================================================

void append_tick(
    std::vector<AxisTick>& ticks,
    int position,
    float confidence = 0.50f
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

    std::vector<int> projection(
        static_cast<std::size_t>(width),
        0
    );

    for (
        int x = axis.start_x;
        x <= axis.end_x;
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
            static_cast<std::size_t>(x)
        ] = count;
    }

    const int required_signal =
        std::max(
            1,
            MAX_TICK_SEARCH_DISTANCE / 4
        );

    int run_start = -1;
    int peak = -1;
    int peak_value = 0;

    for (
        int x = axis.start_x;
        x <= axis.end_x;
        ++x
    ) {

        const int value =
            projection[
                static_cast<std::size_t>(x)
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
                value >
                peak_value
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

    std::vector<int> projection(
        static_cast<std::size_t>(height),
        0
    );

    for (
        int y = axis.start_y;
        y <= axis.end_y;
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
            static_cast<std::size_t>(y)
        ] = count;
    }

    const int required_signal =
        std::max(
            1,
            MAX_TICK_SEARCH_DISTANCE / 4
        );

    int run_start = -1;
    int peak = -1;
    int peak_value = 0;

    for (
        int y = axis.start_y;
        y <= axis.end_y;
        ++y
    ) {

        const int value =
            projection[
                static_cast<std::size_t>(y)
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
                value >
                peak_value
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
// THICKNESS
// =============================================================================

[[nodiscard]]
int estimate_horizontal_thickness(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    int y
) noexcept
{
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
            horizontal_span(
                image,
                width,
                above,
                channels
            ).coverage(width);

        const double below_coverage =
            horizontal_span(
                image,
                width,
                below,
                channels
            ).coverage(width);

        if (
            above_coverage >=
                MIN_AXIS_DENSITY * 0.5 &&
            below_coverage >=
                MIN_AXIS_DENSITY * 0.5
        ) {
            thickness =
                delta * 2 + 1;
        }
    }

    return thickness;
}

[[nodiscard]]
int estimate_vertical_thickness(
    const std::uint8_t* image,
    int width,
    int height,
    int channels,
    int x
) noexcept
{
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
            vertical_span(
                image,
                width,
                height,
                left,
                channels
            ).coverage(height);

        const double right_coverage =
            vertical_span(
                image,
                width,
                height,
                right,
                channels
            ).coverage(height);

        if (
            left_coverage >=
                MIN_AXIS_DENSITY * 0.5 &&
            right_coverage >=
                MIN_AXIS_DENSITY * 0.5
        ) {
            thickness =
                delta * 2 + 1;
        }
    }

    return thickness;
}

} // namespace

// =============================================================================
// HORIZONTAL AXIS
// =============================================================================

ChartAxis ChartAxisDetector::detect_horizontal_axis(
    const std::uint8_t* image,
    int width,
    int height,
    int channels
) const
{
    ChartAxis result{};

    result.kind =
        AxisKind::X_AXIS;

    if (
        !valid_image(
            image,
            width,
            height,
            channels
        )
    ) {
        return result;
    }

    AxisCandidate best{};

    for (
        int y = EDGE_MARGIN;
        y < height - EDGE_MARGIN;
        ++y
    ) {

        const SpanStats stats =
            horizontal_span(
                image,
                width,
                y,
                channels
            );

        const int extent =
            stats.extent();

        if (
            extent <
            MIN_AXIS_LENGTH_PIXELS
        ) {
            continue;
        }

        const double coverage =
            stats.coverage(width);

        const double density =
            stats.density();

        if (
            coverage <
            MIN_HORIZONTAL_COVERAGE ||
            density <
            MIN_AXIS_DENSITY
        ) {
            continue;
        }

        const double base_score =
            axis_score(
                stats,
                width
            );

        const double lower_prior =
            static_cast<double>(y) /
            static_cast<double>(height);

        const double score =
            std::clamp(
                base_score +
                lower_prior * 0.15,
                0.0,
                1.0
            );

        if (
            !best.valid() ||
            score >
                best.score
        ) {

            best.position = y;
            best.start = stats.first;
            best.end = stats.last;
            best.coverage = coverage;
            best.density = density;
            best.score = score;
        }
    }

    if (
        !best.valid()
    ) {
        return result;
    }

    result.start_x =
        best.start;

    result.start_y =
        best.position;

    result.end_x =
        best.end;

    result.end_y =
        best.position;

    result.thickness =
        estimate_horizontal_thickness(
            image,
            width,
            height,
            channels,
            best.position
        );

    result.horizontal = true;
    result.vertical = false;

    result.confidence =
        static_cast<float>(
            std::clamp(
                best.score,
                0.0,
                1.0
            )
        );

    detect_horizontal_ticks(
        image,
        width,
        height,
        channels,
        result
    );

    return result;
}

// =============================================================================
// VERTICAL AXIS
// =============================================================================

ChartAxis ChartAxisDetector::detect_vertical_axis(
    const std::uint8_t* image,
    int width,
    int height,
    int channels
) const
{
    ChartAxis result{};

    result.kind =
        AxisKind::Y_AXIS;

    if (
        !valid_image(
            image,
            width,
            height,
            channels
        )
    ) {
        return result;
    }

    AxisCandidate best{};

    for (
        int x = EDGE_MARGIN;
        x < width - EDGE_MARGIN;
        ++x
    ) {

        const SpanStats stats =
            vertical_span(
                image,
                width,
                height,
                x,
                channels
            );

        const int extent =
            stats.extent();

        if (
            extent <
            MIN_AXIS_LENGTH_PIXELS
        ) {
            continue;
        }

        const double coverage =
            stats.coverage(height);

        const double density =
            stats.density();

        if (
            coverage <
            MIN_VERTICAL_COVERAGE ||
            density <
            MIN_AXIS_DENSITY
        ) {
            continue;
        }

        const double base_score =
            axis_score(
                stats,
                height
            );

        /*
         * Conventional Cartesian charts generally place the Y axis toward
         * the left side of the image.
         */
        const double left_prior =
            1.0 -
            (
                static_cast<double>(x) /
                static_cast<double>(width)
            );

        const double score =
            std::clamp(
                base_score +
                left_prior * 0.15,
                0.0,
                1.0
            );

        if (
            !best.valid() ||
            score >
                best.score
        ) {

            best.position = x;
            best.start = stats.first;
            best.end = stats.last;
            best.coverage = coverage;
            best.density = density;
            best.score = score;
        }
    }

    if (
        !best.valid()
    ) {
        return result;
    }

    result.start_x =
        best.position;

    result.start_y =
        best.start;

    result.end_x =
        best.position;

    result.end_y =
        best.end;

    result.thickness =
        estimate_vertical_thickness(
            image,
            width,
            height,
            channels,
            best.position
        );

    result.horizontal = false;
    result.vertical = true;

    result.confidence =
        static_cast<float>(
            std::clamp(
                best.score,
                0.0,
                1.0
            )
        );

    detect_vertical_ticks(
        image,
        width,
        height,
        channels,
        result
    );

    return result;
}

// =============================================================================
// PLOT AREA
// =============================================================================

PlotArea ChartAxisDetector::detect_plot_area(
    const ChartAxis& x_axis,
    const ChartAxis& y_axis,
    int width,
    int height
) const
{
    PlotArea result{};

    if (
        width <= 0 ||
        height <= 0 ||
        !x_axis.horizontal ||
        !y_axis.vertical
    ) {
        return result;
    }

    const int left =
        std::clamp(
            y_axis.start_x,
            0,
            width - 1
        );

    const int right =
        std::clamp(
            x_axis.end_x,
            left,
            width - 1
        );

    const int bottom =
        std::clamp(
            x_axis.start_y,
            0,
            height - 1
        );

    const int top =
        std::clamp(
            y_axis.start_y,
            0,
            bottom
        );

    if (
        right <= left ||
        bottom <= top
    ) {
        return result;
    }

    result.min_x = left;
    result.min_y = top;
    result.max_x = right;
    result.max_y = bottom;

    const double area =
        static_cast<double>(
            right - left
        ) *
        static_cast<double>(
            bottom - top
        );

    const double image_area =
        static_cast<double>(width) *
        static_cast<double>(height);

    result.confidence =
        image_area > 0.0
            ? static_cast<float>(
                  std::clamp(
                      area / image_area,
                      0.0,
                      1.0
                  )
              )
            : 0.0f;

    return result;
}

// =============================================================================
// TICK DISPATCH
// =============================================================================

void ChartAxisDetector::detect_ticks(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    ChartAxis& axis
) const
{
    if (
        !valid_image(
            chart_buffer,
            width,
            height,
            channels
        )
    ) {
        axis.ticks.clear();
        return;
    }

    if (
        axis.horizontal
    ) {

        detect_horizontal_ticks(
            chart_buffer,
            width,
            height,
            channels,
            axis
        );

    } else if (
        axis.vertical
    ) {

        detect_vertical_ticks(
            chart_buffer,
            width,
            height,
            channels,
            axis
        );

    } else {

        axis.ticks.clear();
    }
}

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
        !valid_image(
            chart_buffer,
            width,
            height,
            channels
        )
    ) {
        return result;
    }

    result.x_axis =
        detect_horizontal_axis(
            chart_buffer,
            width,
            height,
            channels
        );

    result.y_axis =
        detect_vertical_axis(
            chart_buffer,
            width,
            height,
            channels
        );

    /*
     * detect_horizontal_axis() and detect_vertical_axis() already populate
     * ticks. Calling the public dispatch again is unnecessary and would only
     * rescan the image.
     */

    result.plot_area =
        detect_plot_area(
            result.x_axis,
            result.y_axis,
            width,
            height
        );

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
