#include "fin_ocr/chart/object/object_geometry.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace fin_ocr::chart::object {

namespace {

// =============================================================================
// CONFIGURATION
// =============================================================================

constexpr int MIN_RECT_WIDTH =
    config::CHART_OBJECT_MIN_RECT_WIDTH;

constexpr int MIN_RECT_HEIGHT =
    config::CHART_OBJECT_MIN_RECT_HEIGHT;

constexpr double MIN_OBJECT_DENSITY =
    config::CHART_OBJECT_MIN_DENSITY;

// =============================================================================
// OBJECT SIGNAL THRESHOLD
// =============================================================================
//
// This preserves the threshold used by the original monolithic detector.
//
// It is intentionally independent from CHART_FOREGROUND_THRESHOLD because
// object geometry may come from either OCR foreground strength or chroma.
//
// =============================================================================

constexpr std::uint8_t OBJECT_SIGNAL_THRESHOLD = 24;

} // namespace

// =============================================================================
// GENERAL VALIDATION
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
// PIXEL ACCESS
// =============================================================================

std::size_t pixel_offset(
    int x,
    int y,
    int width,
    int channels
) noexcept
{
    return
        (
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)
        ) *
        static_cast<std::size_t>(channels);
}

// =============================================================================
// OBJECT SIGNAL
// =============================================================================

std::uint8_t geometry_signal(
    const std::uint8_t* image,
    int x,
    int y,
    int width,
    int channels
) noexcept
{
    if (
        image == nullptr ||
        x < 0 ||
        y < 0 ||
        x >= width ||
        channels <= 0
    ) {
        return 0;
    }

    const std::size_t index =
        pixel_offset(
            x,
            y,
            width,
            channels
        );

    // -------------------------------------------------------------------------
    // Single-channel signal.
    // -------------------------------------------------------------------------

    if (
        channels == 1
    ) {
        return image[index];
    }

    // -------------------------------------------------------------------------
    // ChartColorIsolator representation.
    // -------------------------------------------------------------------------

    if (
        channels >= 3
    ) {
        const std::uint8_t foreground =
            image[index + 1];

        const std::uint8_t chroma =
            image[index + 2];

        return
            std::max(
                foreground,
                chroma
            );
    }

    // -------------------------------------------------------------------------
    // Two-channel fallback.
    // -------------------------------------------------------------------------

    return image[index];
}

// =============================================================================
// OBJECT PIXEL
// =============================================================================

bool is_object_pixel(
    const std::uint8_t* image,
    int x,
    int y,
    int width,
    int channels
) noexcept
{
    return
        geometry_signal(
            image,
            x,
            y,
            width,
            channels
        ) >=
        OBJECT_SIGNAL_THRESHOLD;
}

// =============================================================================
// RECTANGLE VALIDATION
// =============================================================================

bool valid_rect(
    int min_x,
    int min_y,
    int max_x,
    int max_y
) noexcept
{
    if (
        max_x < min_x ||
        max_y < min_y
    ) {
        return false;
    }

    const int rect_width =
        max_x -
        min_x +
        1;

    const int rect_height =
        max_y -
        min_y +
        1;

    return
        rect_width >= MIN_RECT_WIDTH &&
        rect_height >= MIN_RECT_HEIGHT;
}

// =============================================================================
// RECTANGLE OVERLAP
// =============================================================================

double intersection_over_union(
    const ChartRect& a,
    const ChartRect& b
) noexcept
{
    const int min_x =
        std::max(
            a.min_x,
            b.min_x
        );

    const int min_y =
        std::max(
            a.min_y,
            b.min_y
        );

    const int max_x =
        std::min(
            a.max_x,
            b.max_x
        );

    const int max_y =
        std::min(
            a.max_y,
            b.max_y
        );

    // -------------------------------------------------------------------------
    // No intersection.
    // -------------------------------------------------------------------------

    if (
        max_x < min_x ||
        max_y < min_y
    ) {
        return 0.0;
    }

    const double intersection =
        static_cast<double>(
            max_x -
            min_x +
            1
        ) *
        static_cast<double>(
            max_y -
            min_y +
            1
        );

    const double area_a =
        static_cast<double>(
            a.max_x -
            a.min_x +
            1
        ) *
        static_cast<double>(
            a.max_y -
            a.min_y +
            1
        );

    const double area_b =
        static_cast<double>(
            b.max_x -
            b.min_x +
            1
        ) *
        static_cast<double>(
            b.max_y -
            b.min_y +
            1
        );

    const double union_area =
        area_a +
        area_b -
        intersection;

    if (
        union_area <= 0.0
    ) {
        return 0.0;
    }

    return
        intersection /
        union_area;
}

// =============================================================================
// PLOT CONTAINMENT
// =============================================================================

bool inside_plot(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    int x,
    int y
) noexcept
{
    // -------------------------------------------------------------------------
    // No reliable coordinate system:
    //
    // Do not reject geometry solely because plot bounds are unavailable.
    // -------------------------------------------------------------------------

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

} // namespace fin_ocr::chart::object
