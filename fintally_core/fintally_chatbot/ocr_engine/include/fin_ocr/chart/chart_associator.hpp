#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "chart_axis_detector.hpp"
#include "chart_object_detector.hpp"

namespace fin_ocr::chart {

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
//
// Gives the interpreter an unambiguous object reference.
//
// object_index is interpreted together with object_kind.
//
// Example:
//
//     object_kind = COLUMN
//     object_index = 4
//
// means:
//
//     ChartObjectSet::bars[4]
//
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
//
// A label is produced by the OCR / recognition layer.
//
// The associator does not own OCR and does not perform text recognition.
// It only classifies and associates already-recognized labels.
//
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
//
// The association layer produces typed semantic edges:
//
//     label
//       |
//       +--> object
//              |
//              +--> series
//              |
//              +--> category
//              |
//              +--> stack
//
// object_index is only meaningful together with object_kind.
//
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
//
// Semantic output consumed by ChartInterpreter.
//
// Typical flow:
//
//     OCR labels
//          |
//          v
//     classification
//          |
//          +----> categories
//          |
//          +----> series
//          |
//          v
//     object association
//          |
//          +----> bars / columns
//          +----> stacks
//          +----> paths / areas
//          +----> radial slices
//          +----> scatter / bubbles
//          +----> waterfall
//          +----> funnel
//          +----> treemap
//
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
// CHART ASSOCIATOR
// =============================================================================

class ChartAssociator {
public:

    ChartAssociator() = default;

    ~ChartAssociator() = default;

    ChartAssociator(
        const ChartAssociator&
    ) = default;

    ChartAssociator& operator=(
        const ChartAssociator&
    ) = default;

    ChartAssociator(
        ChartAssociator&&
    ) noexcept = default;

    ChartAssociator& operator=(
        ChartAssociator&&
    ) noexcept = default;

    // =========================================================================
    // MAIN ASSOCIATION ENTRY POINT
    // =========================================================================

    [[nodiscard]]
    ChartAssociationResult associate(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels
    ) const;

private:

    // =========================================================================
    // LABEL CLASSIFICATION
    // =========================================================================

    void classify_labels(
        const ChartCoordinateSystem& coordinates,
        std::vector<ChartLabel>& labels
    ) const;

    // =========================================================================
    // AXIS / CATEGORY ASSOCIATION
    // =========================================================================

    void associate_axis_labels(
        const ChartCoordinateSystem& coordinates,
        std::vector<ChartLabel>& labels,
        std::vector<ChartCategory>& categories
    ) const;

    // =========================================================================
    // SERIES / LEGEND ASSOCIATION
    // =========================================================================

    void associate_series_labels(
        const ChartCoordinateSystem& coordinates,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartSeries>& series
    ) const;

    // =========================================================================
    // BAR / COLUMN ASSOCIATION
    // =========================================================================
    //
    // Handles:
    //
    //     BAR
    //     COLUMN
    //     STACKED_BAR_SEGMENT
    //     STACKED_COLUMN_SEGMENT
    //
    // Associations identify category / series / stack relationships.
    // Monetary values are not invented here.
    // =========================================================================

    void associate_bars(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartAssociation>& associations
    ) const;

    // =========================================================================
    // LINE / AREA ASSOCIATION
    // =========================================================================
    //
    // Handles:
    //
    //     LINE_SEGMENT
    //     AREA_REGION
    //     STACKED_AREA_REGION
    //
    // =========================================================================

    void associate_paths(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartAssociation>& associations
    ) const;

    // =========================================================================
    // RADIAL ASSOCIATION
    // =========================================================================
    //
    // Handles:
    //
    //     PIE_SLICE
    //     DONUT_SLICE
    //
    // Associations primarily connect nearby DATA_LABEL / LEGEND_LABEL
    // instances to radial slices.
    // =========================================================================

    void associate_radial_objects(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartAssociation>& associations
    ) const;

    // =========================================================================
    // SCATTER / BUBBLE ASSOCIATION
    // =========================================================================
    //
    // Handles:
    //
    //     SCATTER_POINT
    //     BUBBLE
    //
    // Series assignment remains label-driven. The associator does not
    // fabricate series names when no evidence exists.
    // =========================================================================

    void associate_scatter_objects(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartAssociation>& associations
    ) const;

    // =========================================================================
    // WATERFALL ASSOCIATION
    // =========================================================================

    void associate_waterfall_objects(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartAssociation>& associations
    ) const;

    // =========================================================================
    // FUNNEL ASSOCIATION
    // =========================================================================

    void associate_funnel_objects(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartAssociation>& associations
    ) const;

    // =========================================================================
    // TREEMAP ASSOCIATION
    // =========================================================================
    //
    // Treemap hierarchy is represented through the detected node relationships.
    // The associator connects labels to the correct node rather than inferring
    // hierarchy from OCR text.
    // =========================================================================

    void associate_treemap_objects(
        const ChartCoordinateSystem& coordinates,
        const ChartObjectSet& objects,
        const std::vector<ChartLabel>& labels,
        std::vector<ChartAssociation>& associations
    ) const;

    // =========================================================================
    // STACK ASSOCIATION
    // =========================================================================
    //
    // Groups compatible bar / column segments into logical stacks.
    //
    // This pass is geometry-based. It does not calculate percentages.
    // =========================================================================

    void associate_stacked_objects(
        const ChartCoordinateSystem& coordinates,
        ChartObjectSet& objects
    ) const;

    // =========================================================================
    // DUAL-AXIS ASSOCIATION
    // =========================================================================
    //
    // Associates objects with primary / secondary coordinate systems where
    // those axes have actually been detected.
    // =========================================================================

    void associate_dual_axes(
        const ChartCoordinateSystem& coordinates,
        ChartObjectSet& objects
    ) const;

    // =========================================================================
    // GEOMETRIC HELPERS
    // =========================================================================

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
