#include "fin_ocr/chart/coordinate/plot_area.hpp"

#include <algorithm>

namespace fin_ocr::chart::coordinate {

// =============================================================================
// PLOT AREA
// =============================================================================

PlotArea detect_plot_area(
    const fin_ocr::chart::ChartAxis& x_axis,
    const fin_ocr::chart::ChartAxis& y_axis,
    int width,
    int height
) noexcept
{
    PlotArea result{};

    // -------------------------------------------------------------------------
    // Validate image dimensions and axis orientation.
    // -------------------------------------------------------------------------

    if (
        width <= 0 ||
        height <= 0 ||
        !x_axis.horizontal ||
        !y_axis.vertical
    ) {
        return result;
    }

    // -------------------------------------------------------------------------
    // Determine horizontal bounds.
    //
    // Left boundary:
    //     vertical Y axis
    //
    // Right boundary:
    //     horizontal X axis endpoint
    // -------------------------------------------------------------------------

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

    // -------------------------------------------------------------------------
    // Determine vertical bounds.
    //
    // Bottom boundary:
    //     horizontal X axis
    //
    // Top boundary:
    //     vertical Y axis
    // -------------------------------------------------------------------------

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

    // -------------------------------------------------------------------------
    // Reject degenerate regions.
    // -------------------------------------------------------------------------

    if (
        right <= left ||
        bottom <= top
    ) {
        return result;
    }

    // -------------------------------------------------------------------------
    // Store plot bounds.
    // -------------------------------------------------------------------------

    result.min_x = left;
    result.min_y = top;

    result.max_x = right;
    result.max_y = bottom;

    // -------------------------------------------------------------------------
    // Confidence.
    //
    // Plot-area confidence is currently based on the fraction of the image
    // occupied by the detected drawable region.
    //
    // This is deliberately geometry-only. Axis confidence and OCR confidence
    // can be incorporated by a higher-level coordinate-system stage.
    // -------------------------------------------------------------------------

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

} // namespace fin_ocr::chart::coordinate
