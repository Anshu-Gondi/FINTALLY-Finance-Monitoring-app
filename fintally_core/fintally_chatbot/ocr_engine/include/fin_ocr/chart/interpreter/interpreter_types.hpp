#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"
#include "fin_ocr/chart/coordinate/coordinate_system.hpp"
#include "fin_ocr/chart/object/object_types.hpp"

namespace fin_ocr::chart::interpreter {

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

    ChartType type =
        ChartType::UNKNOWN;

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

    ::fin_ocr::chart::ChartCoordinateSystem coordinates;

    ::fin_ocr::chart::object::ChartObjectSet objects;

    ::fin_ocr::chart::association::ChartAssociationResult associations;

    std::vector<ChartDataPoint> data_points;

    bool valid = false;

    float confidence = 0.0f;
};

} // namespace fin_ocr::chart::interpreter
