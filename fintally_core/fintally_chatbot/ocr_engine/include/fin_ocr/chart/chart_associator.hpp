#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "chart_axis_detector.hpp"
#include "chart_object_detector.hpp"

namespace fin_ocr::chart {

// =============================================================================
// CHART LABEL
// =============================================================================

enum class ChartLabelKind : std::uint8_t {

    UNKNOWN = 0,

    X_AXIS_LABEL,
    Y_AXIS_LABEL,

    SERIES_LABEL,

    LEGEND_LABEL,

    DATA_LABEL,

    TITLE,

    SUBTITLE,

    KPI_LABEL,

    TABLE_HEADER,

    TABLE_ROW_LABEL,

    TABLE_CELL
};

struct ChartLabel {

    std::string text;

    int min_x = 0;
    int min_y = 0;

    int max_x = 0;
    int max_y = 0;

    ChartLabelKind kind = ChartLabelKind::UNKNOWN;

    int series_index = -1;
    int category_index = -1;

    float confidence = 0.0f;
};

// =============================================================================
// LABEL-OBJECT ASSOCIATION
// =============================================================================

struct ChartAssociation {

    int label_index = -1;
    int object_index = -1;

    int series_index = -1;
    int category_index = -1;

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

// =============================================================================
// ASSOCIATOR
// =============================================================================

class ChartAssociator {
public:

    [[nodiscard]]
    ChartAssociationResult associate(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels
    ) const;

private:

    void classify_labels(
        const ChartCoordinateSystem& coordinates,
        std::vector<ChartLabel>& labels
    ) const;

    void associate_axis_labels(
        const ChartCoordinateSystem& coordinates,
        std::vector<ChartLabel>& labels,
        std::vector<ChartCategory>& categories
    ) const;

    void associate_series_labels(
        const ChartCoordinateSystem& coordinates,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartSeries>& series
    ) const;

    void associate_bars(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartAssociation>& associations
    ) const;

    void associate_paths(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartAssociation>& associations
    ) const;

    [[nodiscard]]
    static double center_distance(
        int ax,
        int ay,
        int bx,
        int by
    ) noexcept;

    [[nodiscard]]
    static bool horizontal_overlap(
        int a_min_x,
        int a_max_x,
        int b_min_x,
        int b_max_x
    ) noexcept;

    [[nodiscard]]
    static bool vertical_overlap(
        int a_min_y,
        int a_max_y,
        int b_min_y,
        int b_max_y
    ) noexcept;
};

} // namespace fin_ocr::chart
