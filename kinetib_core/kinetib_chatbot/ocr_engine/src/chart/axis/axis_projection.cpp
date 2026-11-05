#include "fin_ocr/chart/axis/axis_projection.hpp"
#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fin_ocr::chart::axis {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr std::uint8_t FOREGROUND_THRESHOLD =
    config::CHART_FOREGROUND_THRESHOLD;

// =============================================================================
// PIXEL OFFSET
// =============================================================================
//
// Computes:
//
//     offset = ((y * width) + x) * channels
//
// without signed arithmetic overflow.
//
// This is kept private because callers should use `is_foreground()` rather
// than depending on the underlying memory layout.
//
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

    // -------------------------------------------------------------------------
    // y * width
    // -------------------------------------------------------------------------

    if (
        sy >
        std::numeric_limits<std::size_t>::max() / sw
    ) {
        return false;
    }

    const std::size_t row_offset =
        sy * sw;

    // -------------------------------------------------------------------------
    // row_offset + x
    // -------------------------------------------------------------------------

    if (
        sx >
        std::numeric_limits<std::size_t>::max() -
            row_offset
    ) {
        return false;
    }

    const std::size_t pixel =
        row_offset + sx;

    // -------------------------------------------------------------------------
    // pixel * channels
    // -------------------------------------------------------------------------

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

} // namespace

// =============================================================================
// SPAN STATS — EXTENT
// =============================================================================

int SpanStats::extent() const noexcept
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

// =============================================================================
// SPAN STATS — COVERAGE
// =============================================================================

double SpanStats::coverage(
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

// =============================================================================
// SPAN STATS — DENSITY
// =============================================================================

double SpanStats::density() const noexcept
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

// =============================================================================
// IMAGE VALIDATION
// =============================================================================

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
// FOREGROUND
// =============================================================================

bool is_foreground(
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
// HORIZONTAL SPAN
// =============================================================================

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

} // namespace fin_ocr::chart::axis
