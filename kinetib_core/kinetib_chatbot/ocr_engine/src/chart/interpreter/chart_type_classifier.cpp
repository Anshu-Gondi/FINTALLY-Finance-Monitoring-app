#include "fin_ocr/chart/interpreter/chart_type_classifier.hpp"

#include "fin_ocr/chart/interpreter/interpreter_geometry.hpp"
#include "fin_ocr/chart/interpreter/stack_classifier.hpp"

#include <algorithm>
#include <cstddef>

namespace fin_ocr::chart::interpreter::classifier {

// =============================================================================
// RADIAL TYPE
// =============================================================================

ChartType classify_radial_type(
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept
{
    if (
        objects.slices.empty()
    ) {
        return ChartType::UNKNOWN;
    }

    bool has_donut =
        false;

    for (
        const ::fin_ocr::chart::object::RadialSlice& slice :
        objects.slices
    ) {
        if (
            slice.inner_radius > 0
        ) {
            has_donut =
                true;

            break;
        }
    }

    return
        has_donut
            ? ChartType::DONUT
            : ChartType::PIE;
}

// =============================================================================
// CARTESIAN TYPE
// =============================================================================

ChartType classify_cartesian_type(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept
{
    const bool has_bars =
        !objects.bars.empty();

    const bool has_paths =
        !objects.paths.empty();

    const bool has_waterfall =
        !objects.waterfall_steps.empty();

    const bool has_funnel =
        !objects.funnel_stages.empty();

    const bool has_scatter =
        !objects.points.empty();

    const bool has_area_objects =
        std::any_of(
            objects.generic_objects.begin(),
            objects.generic_objects.end(),
            [](
                const ::fin_ocr::chart::object::ChartObject& object
            ) noexcept {
                return
                    object.kind ==
                        ::fin_ocr::chart::object::ChartObjectKind::AREA_REGION ||
                    object.kind ==
                        ::fin_ocr::chart::object::ChartObjectKind::STACKED_AREA_REGION;
            }
        );

    // -------------------------------------------------------------------------
    // Specialized Cartesian structures.
    // -------------------------------------------------------------------------

    if (
        has_waterfall
    ) {
        return ChartType::WATERFALL;
    }

    if (
        has_funnel
    ) {
        return ChartType::FUNNEL;
    }

    // -------------------------------------------------------------------------
    // Bar + path combination.
    //
    // Preserve the current legacy classification behavior. The later
    // refinement stage resolves BAR vs COLUMN for the bar component.
    // -------------------------------------------------------------------------

    if (
        has_bars &&
        has_paths
    ) {
        return
            coordinates.has_secondary_y_axis
                ? ChartType::COMBO_LINE_COLUMN
                : ChartType::COMBO_LINE_COLUMN;
    }

    // -------------------------------------------------------------------------
    // Scatter / bubble.
    // -------------------------------------------------------------------------

    if (
        has_scatter &&
        !has_bars &&
        !has_paths
    ) {
        const bool bubble =
            std::any_of(
                objects.points.begin(),
                objects.points.end(),
                [](
                    const ::fin_ocr::chart::object::ScatterPoint& point
                ) noexcept {
                    return point.is_bubble;
                }
            );

        return
            bubble
                ? ChartType::BUBBLE
                : ChartType::SCATTER;
    }

    // -------------------------------------------------------------------------
    // Area / line.
    // -------------------------------------------------------------------------

    if (
        has_area_objects &&
        has_paths
    ) {
        return ChartType::AREA;
    }

    if (
        has_paths
    ) {
        return ChartType::LINE;
    }

    // -------------------------------------------------------------------------
    // Remaining bar/column geometry is refined later.
    // -------------------------------------------------------------------------

    if (
        has_bars
    ) {
        return ChartType::COLUMN;
    }

    return ChartType::UNKNOWN;
}

// =============================================================================
// PRIMARY TYPE
// =============================================================================

ChartType classify_chart_type(
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates,
    const ::fin_ocr::chart::object::ChartObjectSet& objects
) noexcept
{
    // -------------------------------------------------------------------------
    // Radial charts have priority.
    // -------------------------------------------------------------------------

    if (
        !objects.slices.empty()
    ) {
        return
            classify_radial_type(
                objects
            );
    }

    // -------------------------------------------------------------------------
    // Treemap.
    // -------------------------------------------------------------------------

    if (
        objects.treemap_nodes.size() >= 2
    ) {
        return ChartType::TREEMAP;
    }

    // -------------------------------------------------------------------------
    // Funnel.
    // -------------------------------------------------------------------------

    if (
        objects.funnel_stages.size() >= 2
    ) {
        return ChartType::FUNNEL;
    }

    // -------------------------------------------------------------------------
    // Cartesian fallback.
    // -------------------------------------------------------------------------

    return
        classify_cartesian_type(
            coordinates,
            objects
        );
}

// =============================================================================
// BAR CLASSIFICATION REFINEMENT
// =============================================================================
//
// This function extracts the refinement block that was previously embedded in
// ChartInterpreter::interpret().
//
// Priority:
//     combo
//     percent-stacked
//     stacked
//     clustered
//     horizontal bar
//     vertical column
//
// The current behavior intentionally follows the legacy implementation.
//
// =============================================================================

ChartType refine_bar_chart_type(
    ChartType current_type,
    const ::fin_ocr::chart::object::ChartObjectSet& objects,
    bool stacked,
    bool clustered,
    bool percent_stacked,
    bool combo
) noexcept
{
    if (
        objects.bars.empty()
    ) {
        return current_type;
    }

    bool vertical =
        false;

    bool horizontal =
        false;

    for (
        const ::fin_ocr::chart::object::BarSegment& bar :
        objects.bars
    ) {
        if (
            geometry::is_vertical_bar(
                bar
            )
        ) {
            vertical =
                true;
        } else {
            horizontal =
                true;
        }
    }

    // =========================================================================
    // COMBO
    // =========================================================================

    if (
        combo
    ) {
        return
            vertical
                ? ChartType::COMBO_LINE_COLUMN
                : ChartType::COMBO_LINE_BAR;
    }

    // =========================================================================
    // HUNDRED-PERCENT STACKED
    // =========================================================================

    if (
        percent_stacked
    ) {
        return
            vertical
                ? ChartType::HUNDRED_PERCENT_STACKED_COLUMN
                : ChartType::HUNDRED_PERCENT_STACKED_BAR;
    }

    // =========================================================================
    // STACKED
    // =========================================================================

    if (
        stacked
    ) {
        return
            vertical
                ? ChartType::STACKED_COLUMN
                : ChartType::STACKED_BAR;
    }

    // =========================================================================
    // CLUSTERED
    // =========================================================================

    if (
        clustered
    ) {
        return
            vertical
                ? ChartType::CLUSTERED_COLUMN
                : ChartType::CLUSTERED_BAR;
    }

    // =========================================================================
    // HORIZONTAL BAR
    // =========================================================================

    if (
        horizontal
    ) {
        return ChartType::BAR;
    }

    // =========================================================================
    // DEFAULT VERTICAL COLUMN
    // =========================================================================

    return ChartType::COLUMN;
}

} // namespace fin_ocr::chart::interpreter::classifier
