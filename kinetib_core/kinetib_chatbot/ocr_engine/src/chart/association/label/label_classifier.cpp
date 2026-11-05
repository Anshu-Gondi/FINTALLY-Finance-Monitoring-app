#include "fin_ocr/chart/association/label/label_classifier.hpp"

#include "fin_ocr/chart/association/association_geometry.hpp"

#include <algorithm>
#include <vector>

namespace fin_ocr::chart::association::label {

namespace {

// =============================================================================
// LABEL VALIDATION
// =============================================================================
//
// Classification operates only on labels with:
//
//     - non-empty text
//     - valid image-space rectangle
//
// =============================================================================

[[nodiscard]]
bool valid_label(
    const ChartLabel& label
) noexcept
{
    return
        !label.text.empty() &&
        ::fin_ocr::chart::association::geometry::
            valid_rect(
                label.min_x,
                label.min_y,
                label.max_x,
                label.max_y
            );
}

// =============================================================================
// LABEL BELOW HORIZONTAL AXIS
// =============================================================================

[[nodiscard]]
bool label_is_below_axis(
    const ChartLabel& label,
    const ::fin_ocr::chart::ChartAxis& axis
) noexcept
{
    if (
        !axis.horizontal ||
        !valid_label(label)
    ) {
        return false;
    }

    return
        label.min_y >=
        axis.start_y;
}

// =============================================================================
// LABEL LEFT OF VERTICAL AXIS
// =============================================================================

[[nodiscard]]
bool label_is_left_of_axis(
    const ChartLabel& label,
    const ::fin_ocr::chart::ChartAxis& axis
) noexcept
{
    if (
        !axis.vertical ||
        !valid_label(label)
    ) {
        return false;
    }

    return
        label.max_x <=
        axis.start_x;
}

// =============================================================================
// LIKELY TITLE
// =============================================================================
//
// A title is considered likely when:
//
//     - it lies above the detected plot area
//     - its center is reasonably close to the horizontal plot center
//
// =============================================================================

[[nodiscard]]
bool likely_title(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ChartLabel& label
) noexcept
{
    if (
        coordinates.plot_area.max_y <=
        coordinates.plot_area.min_y
    ) {
        return false;
    }

    if (
        label.max_y >=
        coordinates.plot_area.min_y
    ) {
        return false;
    }

    const auto [cx, cy] =
        ::fin_ocr::chart::association::geometry::
            rect_center(
                label.min_x,
                label.min_y,
                label.max_x,
                label.max_y
            );

    (void)cy;

    const int plot_center_x =
        (
            coordinates.plot_area.min_x +
            coordinates.plot_area.max_x
        ) / 2;

    const int plot_width =
        std::max(
            1,
            coordinates.plot_area.max_x -
            coordinates.plot_area.min_x +
            1
        );

    return
        std::abs(
            cx -
            plot_center_x
        ) <=
        plot_width / 4;
}

// =============================================================================
// LIKELY LEGEND
// =============================================================================
//
// Legend labels are expected either:
//
//     - to the right of the plot
//     - above the plot near its upper region
//
// =============================================================================

[[nodiscard]]
bool likely_legend_label(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ChartLabel& label
) noexcept
{
    if (
        !valid_label(label)
    ) {
        return false;
    }

    if (
        coordinates.plot_area.max_x <=
            coordinates.plot_area.min_x ||
        coordinates.plot_area.max_y <=
            coordinates.plot_area.min_y
    ) {
        return false;
    }

    const bool right_of_plot =
        label.min_x >
        coordinates.plot_area.max_x;

    const bool above_plot =
        label.max_y <
        coordinates.plot_area.min_y;

    const bool near_plot_top =
        label.min_y <=
        coordinates.plot_area.min_y +
            96;

    return
        right_of_plot ||
        (
            above_plot &&
            near_plot_top
        );
}

// =============================================================================
// LIKELY DATA LABEL
// =============================================================================
//
// A data label is an already-recognized label whose center lies inside the
// detected Cartesian plot.
//
// =============================================================================

[[nodiscard]]
bool likely_data_label(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ChartLabel& label
) noexcept
{
    if (
        !valid_label(label)
    ) {
        return false;
    }

    if (
        !coordinates.valid
    ) {
        return false;
    }

    const auto [cx, cy] =
        ::fin_ocr::chart::association::geometry::
            rect_center(
                label.min_x,
                label.min_y,
                label.max_x,
                label.max_y
            );

    return
        cx >= coordinates.plot_area.min_x &&
        cx <= coordinates.plot_area.max_x &&
        cy >= coordinates.plot_area.min_y &&
        cy <= coordinates.plot_area.max_y;
}

} // namespace

// =============================================================================
// LABEL CLASSIFICATION
// =============================================================================
//
// Classification order is intentional:
//
//     1. X-axis label
//     2. Y-axis label
//     3. title
//     4. legend / series
//     5. data label
//     6. unknown
//
// Explicit classifications supplied by the OCR/recognition layer are
// preserved.
//
// =============================================================================

void classify_labels(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    std::vector<ChartLabel>& labels
)
{
    for (
        ChartLabel& label :
        labels
    ) {
        // =========================================================================
        // INVALID LABEL
        // =========================================================================

        if (
            !valid_label(label)
        ) {
            label.kind =
                ChartLabelKind::UNKNOWN;

            continue;
        }

        // =========================================================================
        // PRESERVE EXPLICIT CLASSIFICATION
        // =========================================================================
        //
        // The recognition layer may already have supplied a semantic label
        // class. Classification must not overwrite it.
        // =========================================================================

        if (
            label.kind !=
            ChartLabelKind::UNKNOWN
        ) {
            continue;
        }

        // =========================================================================
        // X AXIS
        // =========================================================================

        if (
            coordinates.x_axis.horizontal &&
            label_is_below_axis(
                label,
                coordinates.x_axis
            ) &&
            ::fin_ocr::chart::association::geometry::
                horizontal_overlap(
                    label.min_x,
                    label.max_x,
                    coordinates.x_axis.start_x,
                    coordinates.x_axis.end_x
                )
        ) {
            label.kind =
                ChartLabelKind::X_AXIS_LABEL;

            continue;
        }

        // =========================================================================
        // Y AXIS
        // =========================================================================

        if (
            coordinates.y_axis.vertical &&
            label_is_left_of_axis(
                label,
                coordinates.y_axis
            ) &&
            ::fin_ocr::chart::association::geometry::
                vertical_overlap(
                    label.min_y,
                    label.max_y,
                    coordinates.y_axis.start_y,
                    coordinates.y_axis.end_y
                )
        ) {
            label.kind =
                ChartLabelKind::Y_AXIS_LABEL;

            continue;
        }

        // =========================================================================
        // TITLE
        // =========================================================================

        if (
            likely_title(
                coordinates,
                label
            )
        ) {
            label.kind =
                ChartLabelKind::TITLE;

            continue;
        }

        // =========================================================================
        // LEGEND / SERIES
        // =========================================================================

        if (
            likely_legend_label(
                coordinates,
                label
            )
        ) {
            label.kind =
                ChartLabelKind::LEGEND_LABEL;

            continue;
        }

        // =========================================================================
        // INTERIOR DATA LABEL
        // =========================================================================

        if (
            likely_data_label(
                coordinates,
                label
            )
        ) {
            label.kind =
                ChartLabelKind::DATA_LABEL;

            continue;
        }

        // =========================================================================
        // UNKNOWN
        // =========================================================================

        label.kind =
            ChartLabelKind::UNKNOWN;
    }
}

} // namespace fin_ocr::chart::association::label
