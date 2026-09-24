#include "fin_ocr/chart/axis/axis_scanner.hpp"

#include "fin_ocr/chart/axis/axis_projection.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstdint>

namespace fin_ocr::chart::axis {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr int MIN_AXIS_LENGTH_PIXELS =
    config::CHART_MIN_AXIS_LENGTH_PIXELS;

constexpr double MIN_HORIZONTAL_COVERAGE =
    config::CHART_MIN_HORIZONTAL_AXIS_COVERAGE;

constexpr double MIN_VERTICAL_COVERAGE =
    config::CHART_MIN_VERTICAL_AXIS_COVERAGE;

constexpr double MIN_AXIS_DENSITY =
    config::CHART_MIN_AXIS_DENSITY;

constexpr int EDGE_MARGIN =
    config::CHART_AXIS_EDGE_MARGIN;

// =============================================================================
// AXIS CANDIDATE
// =============================================================================
//
// Internal-only representation.
//
// This is intentionally not exposed through the public chart API.
// It exists only while scanning candidate rows/columns.
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
    bool valid() const noexcept
    {
        return
            position >= 0 &&
            end >= start;
    }
};

} // namespace

// =============================================================================
// HORIZONTAL AXIS
// =============================================================================
//
// Scans image rows for the strongest horizontal axis.
//
// The score combines:
//     - foreground coverage
//     - foreground density
//     - lower-image positional prior
//
// Tick detection and thickness estimation are deliberately performed by
// separate modules.
//
// =============================================================================

ChartAxis AxisScanner::detect_horizontal(
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

    const int y_begin =
        std::max(
            0,
            EDGE_MARGIN
        );

    const int y_end =
        std::max(
            y_begin,
            height - EDGE_MARGIN
        );

    for (
        int y = y_begin;
        y < y_end;
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

        // Conventional Cartesian charts usually place the X axis
        // toward the lower part of the image.
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
            score > best.score
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

    return result;
}

// =============================================================================
// VERTICAL AXIS
// =============================================================================
//
// Scans image columns for the strongest vertical axis.
//
// The score combines:
//     - foreground coverage
//     - foreground density
//     - left-side positional prior
//
// Tick detection and thickness estimation are delegated to their dedicated
// modules.
//
// =============================================================================

ChartAxis AxisScanner::detect_vertical(
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

    const int x_begin =
        std::max(
            0,
            EDGE_MARGIN
        );

    const int x_end =
        std::max(
            x_begin,
            width - EDGE_MARGIN
        );

    for (
        int x = x_begin;
        x < x_end;
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

        // Conventional Cartesian charts usually place the Y axis
        // toward the left side of the image.
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
            score > best.score
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

    return result;
}

} // namespace fin_ocr::chart::axis
