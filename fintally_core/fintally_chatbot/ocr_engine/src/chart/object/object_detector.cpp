#include "fin_ocr/chart/object/object_detector.hpp"

#include "fin_ocr/chart/object/area_detector.hpp"
#include "fin_ocr/chart/object/bar_detector.hpp"
#include "fin_ocr/chart/object/funnel_detector.hpp"
#include "fin_ocr/chart/object/line_detector.hpp"
#include "fin_ocr/chart/object/object_geometry.hpp"
#include "fin_ocr/chart/object/radial_detector.hpp"
#include "fin_ocr/chart/object/scatter_detector.hpp"
#include "fin_ocr/chart/object/treemap_detector.hpp"
#include "fin_ocr/chart/object/waterfall_detector.hpp"

#include <algorithm>
#include <cstddef>

namespace fin_ocr::chart::object {

// =============================================================================
// COMPLETE DETECTOR
// =============================================================================
//
// High-level orchestration only.
//
// Individual detector algorithms live in:
//
//     object::bar
//     object::line
//     object::area
//     object::radial
//     object::scatter
//     object::waterfall
//     object::funnel
//     object::treemap
//
// Shared geometry/component infrastructure is provided by the corresponding
// object modules.
//
// =============================================================================

ChartObjectSet ChartObjectDetector::detect(
    const std::uint8_t* chart_buffer,
    int width,
    int height,
    int channels,
    const ::fin_ocr::chart::ChartCoordinateSystem& coordinates
) const
{
    ChartObjectSet result{};

    // =========================================================================
    // IMAGE VALIDATION
    // =========================================================================

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

    // =========================================================================
    // CARTESIAN OBJECTS
    // =========================================================================
    //
    // These detectors use the coordinate system when one is available:
    //
    //     bars
    //     lines
    //     areas
    //
    // =========================================================================

    bar::detect_bars_and_columns(
        chart_buffer,
        width,
        height,
        channels,
        coordinates,
        result
    );

    line::detect_line_paths(
        chart_buffer,
        width,
        height,
        channels,
        coordinates,
        result
    );

    area::detect_area_regions(
        chart_buffer,
        width,
        height,
        channels,
        coordinates,
        result
    );

    // =========================================================================
    // NON-CARTESIAN / STRUCTURAL OBJECTS
    // =========================================================================

    radial::detect_radial_slices(
        chart_buffer,
        width,
        height,
        channels,
        result
    );

    scatter::detect_scatter_points(
        chart_buffer,
        width,
        height,
        channels,
        coordinates,
        result
    );

    waterfall::detect_waterfall_steps(
        chart_buffer,
        width,
        height,
        channels,
        coordinates,
        result
    );

    funnel::detect_funnel_stages(
        chart_buffer,
        width,
        height,
        channels,
        result
    );

    treemap::detect_treemap_regions(
        chart_buffer,
        width,
        height,
        channels,
        result
    );

    // =========================================================================
    // RESULT VALIDITY
    // =========================================================================

    const std::size_t object_count =
        result.bars.size() +
        result.paths.size() +
        result.slices.size() +
        result.points.size() +
        result.waterfall_steps.size() +
        result.funnel_stages.size() +
        result.treemap_nodes.size() +
        result.generic_objects.size();

    result.valid =
        object_count > 0;

    if (
        !result.valid
    ) {
        result.confidence =
            0.0f;

        return result;
    }

    // =========================================================================
    // GLOBAL CONFIDENCE
    // =========================================================================
    //
    // Aggregate confidence across all emitted object families.
    //
    // This preserves the original monolithic behavior.
    //
    // =========================================================================

    double confidence_sum =
        0.0;

    std::size_t confidence_count =
        0;

    for (
        const BarSegment& object :
        result.bars
    ) {
        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const ChartPath& object :
        result.paths
    ) {
        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const RadialSlice& object :
        result.slices
    ) {
        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const ScatterPoint& object :
        result.points
    ) {
        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const WaterfallStep& object :
        result.waterfall_steps
    ) {
        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const FunnelStage& object :
        result.funnel_stages
    ) {
        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const TreemapNode& object :
        result.treemap_nodes
    ) {
        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    for (
        const ChartObject& object :
        result.generic_objects
    ) {
        confidence_sum +=
            object.confidence;

        ++confidence_count;
    }

    result.confidence =
        confidence_count == 0
            ? 0.0f
            : static_cast<float>(
                  std::clamp(
                      confidence_sum /
                          static_cast<double>(
                              confidence_count
                          ),
                      0.0,
                      1.0
                  )
              );

    return result;
}

} // namespace fin_ocr::chart::object
