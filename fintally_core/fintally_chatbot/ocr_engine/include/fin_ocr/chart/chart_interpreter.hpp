#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "chart_axis_detector.hpp"
#include "chart_associator.hpp"
#include "chart_object_detector.hpp"

namespace fin_ocr::chart {

// =============================================================================
// CHART TYPE
// =============================================================================

enum class ChartType : std::uint8_t {

    UNKNOWN = 0,

    // Comparison / ranking.
    BAR,
    COLUMN,
    STACKED_BAR,
    STACKED_COLUMN,
    HUNDRED_PERCENT_STACKED_BAR,
    HUNDRED_PERCENT_STACKED_COLUMN,
    CLUSTERED_BAR,
    CLUSTERED_COLUMN,
    RIBBON,

    // Trends.
    LINE,
    AREA,
    STACKED_AREA,
    COMBO_LINE_BAR,
    COMBO_LINE_COLUMN,

    // Composition.
    PIE,
    DONUT,
    TREEMAP,
    WATERFALL,
    FUNNEL,

    // Correlation / distribution.
    SCATTER,
    BUBBLE,

    // KPI.
    CARD,
    MULTI_ROW_CARD,
    KPI,
    GAUGE,

    // Tables.
    TABLE,
    MATRIX
};

// =============================================================================
// VALUE
// =============================================================================

struct ChartValue {

    double value = 0.0;

    bool valid = false;

    float confidence = 0.0f;
};

// =============================================================================
// DATA POINT
// =============================================================================

struct ChartDataPoint {

    std::string category;

    std::string series;

    ChartValue value;

    int category_index = -1;

    int series_index = -1;

    int object_index = -1;

    float confidence = 0.0f;
};

// =============================================================================
// CHART METADATA
// =============================================================================

struct ChartMetadata {

    ChartType type = ChartType::UNKNOWN;

    std::string title;

    std::string x_axis_title;
    std::string y_axis_title;

    bool has_x_axis = false;
    bool has_y_axis = false;

    bool has_secondary_y_axis = false;

    float confidence = 0.0f;
};

// =============================================================================
// CHART ANALYSIS
// =============================================================================

struct ChartAnalysis {

    ChartMetadata metadata;

    ChartCoordinateSystem coordinates;

    ChartObjectSet objects;

    ChartAssociationResult associations;

    std::vector<ChartDataPoint> data_points;

    bool valid = false;

    float confidence = 0.0f;
};

// =============================================================================
// INTERPRETER
// =============================================================================

class ChartInterpreter {
public:

    [[nodiscard]]
    ChartAnalysis interpret(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const ChartAssociationResult& associations
    ) const;

private:

    [[nodiscard]]
    ChartType classify_chart_type(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects
    ) const;

    [[nodiscard]]
    bool is_stacked(
        const ChartObjectSet& objects
    ) const noexcept;

    [[nodiscard]]
    bool is_percent_stacked(
        const ChartObjectSet& objects
    ) const noexcept;

    [[nodiscard]]
    bool is_clustered(
        const ChartObjectSet& objects
    ) const noexcept;

    [[nodiscard]]
    bool is_combo(
        const ChartObjectSet& objects
    ) const noexcept;

    [[nodiscard]]
    std::vector<ChartDataPoint>
    build_bar_data(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const ChartAssociationResult& associations
    ) const;

    [[nodiscard]]
    std::vector<ChartDataPoint>
    build_line_data(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const ChartAssociationResult& associations
    ) const;

    [[nodiscard]]
    std::vector<ChartDataPoint>
    build_radial_data(
        const ChartObjectSet& objects,
        const ChartAssociationResult& associations
    ) const;

    [[nodiscard]]
    std::vector<ChartDataPoint>
    build_scatter_data(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const ChartAssociationResult& associations
    ) const;

    [[nodiscard]]
    std::vector<ChartDataPoint>
    build_waterfall_data(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const ChartAssociationResult& associations
    ) const;

    [[nodiscard]]
    std::vector<ChartDataPoint>
    build_funnel_data(
        const ChartObjectSet& objects,
        const ChartAssociationResult& associations
    ) const;

    [[nodiscard]]
    std::vector<ChartDataPoint>
    build_treemap_data(
        const ChartObjectSet& objects,
        const ChartAssociationResult& associations
    ) const;

    [[nodiscard]]
    double pixel_to_value(
        const ChartAxis& axis,
        int pixel
    ) const noexcept;

    [[nodiscard]]
    double estimate_scale(
        const ChartAxis& axis
    ) const noexcept;
};

} // namespace fin_ocr::chart
