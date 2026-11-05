#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fin_ocr::chart::association {

// =============================================================================
// CHART LABEL KIND
// =============================================================================

enum class ChartLabelKind : std::uint8_t {

    UNKNOWN = 0,

    // Axis / category labels.
    X_AXIS_LABEL,
    Y_AXIS_LABEL,

    // Series / legend labels.
    SERIES_LABEL,
    LEGEND_LABEL,

    // Object-local labels.
    DATA_LABEL,

    // Document hierarchy.
    TITLE,
    SUBTITLE,

    // KPI / metric labels.
    KPI_LABEL,

    // Tabular structures.
    TABLE_HEADER,
    TABLE_ROW_LABEL,
    TABLE_CELL
};

// =============================================================================
// ASSOCIATED OBJECT KIND
// =============================================================================

enum class AssociatedObjectKind : std::uint8_t {

    UNKNOWN = 0,

    BAR,
    COLUMN,

    STACKED_BAR_SEGMENT,
    STACKED_COLUMN_SEGMENT,

    LINE_SEGMENT,
    AREA_REGION,
    STACKED_AREA_REGION,

    PIE_SLICE,
    DONUT_SLICE,

    SCATTER_POINT,
    BUBBLE,

    WATERFALL_STEP,
    FUNNEL_STAGE,
    TREEMAP_RECTANGLE,

    CARD_REGION,
    KPI_REGION,
    GAUGE_ARC
};

// =============================================================================
// CHART LABEL
// =============================================================================

struct ChartLabel {

    std::string text;

    int min_x = 0;
    int min_y = 0;

    int max_x = 0;
    int max_y = 0;

    ChartLabelKind kind =
        ChartLabelKind::UNKNOWN;

    int series_index = -1;

    int category_index = -1;

    float confidence = 0.0f;
};

// =============================================================================
// LABEL -> OBJECT ASSOCIATION
// =============================================================================

struct ChartAssociation {

    int label_index = -1;

    AssociatedObjectKind object_kind =
        AssociatedObjectKind::UNKNOWN;

    int object_index = -1;

    int series_index = -1;

    int category_index = -1;

    int stack_index = -1;

    double distance = 0.0;

    float confidence = 0.0f;
};

// =============================================================================
// CATEGORY
// =============================================================================

struct ChartCategory {

    std::string name;

    int label_index = -1;

    int category_index = -1;

    int center_x = 0;
    int center_y = 0;

    float confidence = 0.0f;
};

// =============================================================================
// SERIES
// =============================================================================

struct ChartSeries {

    std::string name;

    int label_index = -1;

    int series_index = -1;

    float confidence = 0.0f;
};

// =============================================================================
// ASSOCIATION RESULT
// =============================================================================

struct ChartAssociationResult {

    std::vector<ChartLabel> labels;

    std::vector<ChartCategory> categories;

    std::vector<ChartSeries> series;

    std::vector<ChartAssociation> associations;

    bool valid = false;

    float confidence = 0.0f;
};

} // namespace fin_ocr::chart::association
