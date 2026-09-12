#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "chart_axis_detector.hpp"

namespace fin_ocr::chart {

// =============================================================================
// CHART OBJECT KIND
// =============================================================================

enum class ChartObjectKind : std::uint8_t {

    UNKNOWN = 0,

    // Comparison / ranking.
    BAR,
    COLUMN,
    STACKED_BAR_SEGMENT,
    STACKED_COLUMN_SEGMENT,
    RIBBON,

    // Trends.
    LINE_SEGMENT,
    AREA_REGION,
    STACKED_AREA_REGION,

    // Composition.
    PIE_SLICE,
    DONUT_SLICE,
    TREEMAP_RECTANGLE,
    WATERFALL_STEP,
    FUNNEL_STAGE,

    // Correlation / distribution.
    SCATTER_POINT,
    BUBBLE,

    // KPI / metric.
    CARD_REGION,
    KPI_REGION,
    GAUGE_ARC,

    // Tabular.
    TABLE_CELL,
    MATRIX_CELL,

    // Generic visual structure.
    GRID_REGION,
    MARKER
};

// =============================================================================
// RECTANGULAR OBJECT
// =============================================================================

struct ChartRect {

    int min_x = 0;
    int min_y = 0;

    int max_x = 0;
    int max_y = 0;

    float confidence = 0.0f;
};

// =============================================================================
// BAR / COLUMN SEGMENT
// =============================================================================

struct BarSegment {

    ChartRect bounds;

    int series_index = -1;
    int category_index = -1;

    int stack_index = -1;

    double inferred_value = 0.0;

    bool has_inferred_value = false;

    bool negative = false;

    float confidence = 0.0f;
};

// =============================================================================
// LINE / AREA GEOMETRY
// =============================================================================

struct ChartPathPoint {

    int x = 0;
    int y = 0;

    float confidence = 0.0f;
};

struct ChartPath {

    int series_index = -1;

    std::vector<ChartPathPoint> points;

    float confidence = 0.0f;
};

// =============================================================================
// PIE / DONUT
// =============================================================================

struct RadialSlice {

    double start_angle = 0.0;
    double end_angle = 0.0;

    double fraction = 0.0;

    int center_x = 0;
    int center_y = 0;

    int inner_radius = 0;
    int outer_radius = 0;

    int category_index = -1;

    float confidence = 0.0f;
};

// =============================================================================
// SCATTER / BUBBLE
// =============================================================================

struct ScatterPoint {

    int x = 0;
    int y = 0;

    double x_value = 0.0;
    double y_value = 0.0;

    bool has_x_value = false;
    bool has_y_value = false;

    double radius = 0.0;

    bool is_bubble = false;

    int series_index = -1;

    float confidence = 0.0f;
};

// =============================================================================
// WATERFALL
// =============================================================================

struct WaterfallStep {

    ChartRect bounds;

    double delta = 0.0;
    double cumulative_value = 0.0;

    bool has_delta = false;
    bool has_cumulative_value = false;

    bool total = false;

    int category_index = -1;

    float confidence = 0.0f;
};

// =============================================================================
// FUNNEL
// =============================================================================

struct FunnelStage {

    ChartRect bounds;

    double relative_width = 0.0;
    double inferred_value = 0.0;

    bool has_inferred_value = false;

    int stage_index = -1;

    float confidence = 0.0f;
};

// =============================================================================
// TREEMAP
// =============================================================================

struct TreemapNode {

    ChartRect bounds;

    int parent_index = -1;
    int hierarchy_level = 0;

    double inferred_value = 0.0;

    bool has_inferred_value = false;

    int label_index = -1;

    float confidence = 0.0f;
};

// =============================================================================
// GENERIC CHART OBJECT
// =============================================================================

struct ChartObject {

    ChartObjectKind kind = ChartObjectKind::UNKNOWN;

    ChartRect bounds;

    int series_index = -1;
    int category_index = -1;

    int label_index = -1;

    double value = 0.0;

    bool has_value = false;

    float confidence = 0.0f;
};

// =============================================================================
// DETECTION RESULT
// =============================================================================

struct ChartObjectSet {

    std::vector<BarSegment> bars;
    std::vector<ChartPath> paths;
    std::vector<RadialSlice> slices;
    std::vector<ScatterPoint> points;
    std::vector<WaterfallStep> waterfall_steps;
    std::vector<FunnelStage> funnel_stages;
    std::vector<TreemapNode> treemap_nodes;

    std::vector<ChartObject> generic_objects;

    bool valid = false;

    float confidence = 0.0f;
};

// =============================================================================
// OBJECT DETECTOR
// =============================================================================

class ChartObjectDetector {
public:

    [[nodiscard]]
    ChartObjectSet detect(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        const ChartCoordinateSystem& coordinates
    ) const;

private:

    void detect_bars_and_columns(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        const ChartCoordinateSystem& coordinates,
        ChartObjectSet& result
    ) const;

    void detect_line_paths(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        const ChartCoordinateSystem& coordinates,
        ChartObjectSet& result
    ) const;

    void detect_area_regions(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        const ChartCoordinateSystem& coordinates,
        ChartObjectSet& result
    ) const;

    void detect_radial_slices(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        ChartObjectSet& result
    ) const;

    void detect_scatter_points(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        const ChartCoordinateSystem& coordinates,
        ChartObjectSet& result
    ) const;

    void detect_waterfall_steps(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        const ChartCoordinateSystem& coordinates,
        ChartObjectSet& result
    ) const;

    void detect_funnel_stages(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        ChartObjectSet& result
    ) const;

    void detect_treemap_regions(
        const std::uint8_t* chart_buffer,
        int width,
        int height,
        int channels,
        ChartObjectSet& result
    ) const;
};

} // namespace fin_ocr::chart
